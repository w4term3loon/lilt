// Run through python3 tests/native_activation.py, never on the desktop bus.
// Keep Gio, proxy initialization, and the extension's call/owner handling real.
const Gio = imports.gi.Gio;
const GLib = imports.gi.GLib;
const System = imports.system;

if (GLib.getenv('LILT_TEST_ISOLATED') !== '1')
    throw new Error('Run this test with tests/native_activation.py for isolation');

const sourcePath = GLib.build_filenamev([
    GLib.path_get_dirname(System.programInvocationName), '..', 'extension', 'extension.js',
]);
const source = imports.byteArray.toString(GLib.file_get_contents(sourcePath)[1])
    .replace(/^import .*;$/gm, '')
    .replace('export default class LiltExtension extends Extension',
        'globalThis.TestExtension = class LiltExtension extends Extension');
const Clutter = {
    KEY_Return: 65293, KEY_KP_Enter: 65421,
    ModifierType: {SHIFT_MASK: 1, CONTROL_MASK: 4, MOD1_MASK: 8, MOD4_MASK: 64, SUPER_MASK: 1 << 26},
};
const Extension = class {};
const Main = {
    layoutManager: {connect() { return 1; }},
    inputMethod: {connect() { return 1; }, currentFocus: null},
};
const IBusManager = {getIBusManager() { return {connect() { return 1; }}; }};
eval(source);

const name = 'io.github.lilt.Dictation';
const path = '/io/github/lilt/Dictation';
const bus = Gio.DBus.session;
const errors = [];

function assert(condition, message) {
    if (!condition)
        throw new Error(message);
}

function daemon(method, parameters) {
    return bus.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus',
        'org.freedesktop.DBus', method, parameters, null,
        Gio.DBusCallFlags.NONE, 3000, null).deepUnpack();
}

function hasOwner() {
    return daemon('NameHasOwner', new GLib.Variant('(s)', [name]))[0];
}

function delay(milliseconds) {
    return new Promise(resolve => GLib.timeout_add(GLib.PRIORITY_DEFAULT,
        milliseconds, () => {
            resolve();
            return GLib.SOURCE_REMOVE;
        }));
}

async function until(predicate, label) {
    const deadline = GLib.get_monotonic_time() + 5_000_000;
    while (!predicate()) {
        if (errors.length)
            throw errors.shift();
        if (GLib.get_monotonic_time() > deadline)
            throw new Error(`Timed out: ${label}`);
        await delay(10);
    }
    if (errors.length)
        throw errors.shift();
}

function nativeCall(method, parameters = null, iface = name) {
    return bus.call_sync(name, path, iface, method, parameters, null,
        Gio.DBusCallFlags.NONE, 5000, null).deepUnpack();
}

async function quitNative() {
    nativeCall('Quit');
    await until(() => !hasOwner(), 'native service release after Quit');
}

function checkStartup(read) {
    const expected = {State: 'idle', Message: 'Ready', Shortcut: '<Control><Alt>space',
        FinishShortcut: '<Shift>F8', LivePreview: false};
    for (const [property, value] of Object.entries(expected))
        assert(read(property) === value, `${property} must be available with its configured value at startup`);
}

function state() {
    return nativeCall('Get', new GLib.Variant('(ss)', [name, 'State']),
        'org.freedesktop.DBus.Properties')[0].deepUnpack();
}

async function checkSingleInstance() {
    const owner = daemon('GetNameOwner', new GLib.Variant('(s)', [name]))[0];
    const child = Gio.Subprocess.new([GLib.getenv('LILT_TEST_BINARY')], Gio.SubprocessFlags.NONE);
    let finished = false;
    child.wait_check_async(null, (process, result) => {
        try { process.wait_check_finish(result); } catch (error) { errors.push(error); }
        finished = true;
    });
    try {
        await until(() => finished, 'second launch forwards to the running instance');
        assert(daemon('GetNameOwner', new GLib.Variant('(s)', [name]))[0] === owner,
            'A second launch must preserve the existing service owner');
    } finally {
        if (!finished)
            child.force_exit();
    }
}

async function run() {
    assert(!hasOwner(), 'Test requires an initially stopped service');
    // The bus queues this first GetAll before activation. The custom interface
    // and saved settings must exist when the service acquires its name.
    const [properties] = nativeCall('GetAll', new GLib.Variant('(s)', [name]),
        'org.freedesktop.DBus.Properties');
    checkStartup(property => properties[property]?.deepUnpack());
    nativeCall('Toggle');
    assert(state() === 'error', 'Dictation without desktop integration must fail closed');
    let rejected = false;
    try { nativeCall('Attach'); } catch (error) {
        rejected = error.message.includes('Only GNOME Shell can attach.');
    }
    assert(rejected, 'An ordinary process must not be able to Attach');
    assert(daemon('RequestName', new GLib.Variant('(su)', ['org.gnome.Shell', 0]))[0] === 1,
        'Test must own the isolated GNOME Shell bus name');
    nativeCall('Attach');
    nativeCall('Cancel');
    for (let cycle = 0; cycle < 2; cycle++) {
        nativeCall('Toggle');
        nativeCall('Cancel');
        await delay(100);
        assert(state() === 'idle', 'Cancel and restart must suppress late worker callbacks');
    }
    nativeCall('Toggle');
    nativeCall('ReportError', new GLib.Variant('(s)', ['Test insertion failure']));
    await delay(200);
    assert(state() === 'error', 'Insertion failure must remain visible after worker cleanup');
    assert(nativeCall('Get', new GLib.Variant('(ss)', [name, 'Message']),
        'org.freedesktop.DBus.Properties')[0].deepUnpack() === 'Test insertion failure',
        'A late capture error must not replace the insertion failure');
    nativeCall('Cancel');
    await checkSingleInstance();
    nativeCall('Detach');
    await quitNative();
    print('Native service passed: first-call activation, trusted Attach, cancellation, persistent errors, single instance');

    const settings = {
        shortcut: '<Control><Super>space',
        connect() { return 1; },
        get_strv() { return [this.shortcut]; },
        set_strv(_key, value) { this.shortcut = value[0]; },
    };
    const extension = new TestExtension();
    Object.assign(extension, {
        getSettings() { return settings; },
        _makeUi() {}, _bindShortcut() {}, _watchIbusPanel() {},
        _drawState() {}, _finishSession() {},
        _error(message) { errors.push(new Error(message)); },
    });
    extension.enable();
    await until(() => extension._proxyReady, 'extension proxy initialization');
    await delay(100);
    assert(!hasOwner(), 'Enabling the extension must leave the service stopped');
    for (let cycle = 0; cycle < 2; cycle++) {
        let complete = false;
        extension._call('Attach', null, () => { complete = true; });
        await until(() => complete && extension._attached,
            `extension Attach after ${cycle ? 'Quit' : 'cold startup'}`);
        checkStartup(property => extension._proxy[property]);
        await quitNative();
        await until(() => !extension._proxy.g_name_owner && !extension._attached,
            'extension observes the service owner disappearing');
    }
    extension._enabled = false;
    for (const signal of [extension._proxyPropertySignal, extension._proxyOwnerSignal]) {
        if (signal)
            extension._proxy.disconnect(signal);
    }
    extension._proxy.disconnectSignal(extension._transcriptSignal);
    extension._proxy.disconnectSignal(extension._partialSignal);
    print('Real extension proxy passed: lazy startup, cold Attach, and reactivation after Quit');
}

const loop = new GLib.MainLoop(null, false);
let exitCode = 0;
run().catch(error => {
    printerr(`${error.message}\n${error.stack || ''}`);
    exitCode = 1;
}).finally(() => loop.quit());
loop.run();
System.exit(exitCode);
