// Run through python3 tests/native_activation.py, never on the desktop bus.
// Keep Gio, proxy initialization, and the extension's call/owner handling real.
const Gio = imports.gi.Gio;
const GLib = imports.gi.GLib;
const System = imports.system;

if (GLib.getenv('LILT_TEST_ISOLATED') !== '1')
    throw new Error('Run this test with tests/native_activation.py for isolation');

const sourcePath = GLib.getenv('LILT_TEST_EXTENSION_SOURCE') || GLib.build_filenamev([
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

async function run() {
    assert(!hasOwner(), 'Test requires an initially stopped service');
    assert(daemon('RequestName', new GLib.Variant('(su)', ['org.gnome.Shell', 0]))[0] === 1,
        'Test must own the isolated GNOME Shell bus name');

    if (!ARGV.includes('--native-only')) {
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

        for (let cycle = 0; cycle < 3; cycle++) {
            let complete = false;
            extension._call('Attach', null, () => { complete = true; });
            await until(() => complete && extension._attached,
                `extension Attach after ${cycle ? 'Quit' : 'cold startup'}`);
            assert(extension._proxy.State === 'idle', 'Initial State must be cached');
            assert(extension._proxy.Shortcut === '<Control><Alt>space',
                'Initial shortcut must be loaded before D-Bus properties are served');
            assert(extension._proxy.FinishShortcut === '<Shift>F8',
                'Configured finish shortcut must be cached independently from Start');
            assert(nativeCall('Get', new GLib.Variant('(ss)', [name, 'LivePreview']),
                'org.freedesktop.DBus.Properties')[0].deepUnpack() === false,
                'Configured preview preference must survive startup');
            assert(extension._proxy.Message === 'Ready', 'Initial Message must be cached');
            const owner = extension._proxy.g_name_owner;
            await quitNative();
            await until(() => !extension._proxy.g_name_owner && !extension._attached,
                'extension observes the service owner disappearing');
            assert(owner, 'Attach must acquire a real D-Bus service owner');
        }
        extension._enabled = false;
        for (const signal of [extension._proxyPropertySignal, extension._proxyOwnerSignal]) {
            if (signal)
                extension._proxy.disconnect(signal);
        }
        extension._proxy.disconnectSignal(extension._transcriptSignal);
        extension._proxy.disconnectSignal(extension._partialSignal);
        print('Real extension proxy passed: lazy startup, cold Attach, and 2 reactivations after Quit');
    }

    // Activation queues this GetAll before the service owns its name. The custom
    // interface must already exist when the bus releases that pending message.
    for (let cycle = 0; cycle < 8; cycle++) {
        assert(!hasOwner(), 'Direct activation cycle must begin without an owner');
        const [properties] = nativeCall('GetAll', new GLib.Variant('(s)', [name]),
            'org.freedesktop.DBus.Properties');
        assert(properties.State?.deepUnpack() === 'idle',
            'Cold GetAll must expose the custom interface immediately');
        assert(properties.Shortcut?.deepUnpack() === '<Control><Alt>space',
            'Cold GetAll must return the configured shortcut');
        assert(properties.FinishShortcut?.deepUnpack() === '<Shift>F8',
            'Cold GetAll must return the configured finish shortcut after restarts');
        assert(properties.LivePreview?.deepUnpack() === false,
            'Cold GetAll must return the configured preview preference after restarts');
        nativeCall('Attach');
        await quitNative();
    }
    print('Native activation passed: 8 immediate cold GetAll/Attach/Quit cycles');
}

const loop = new GLib.MainLoop(null, false);
let exitCode = 0;
run().catch(error => {
    printerr(`${error.message}\n${error.stack || ''}`);
    exitCode = 1;
}).finally(() => loop.quit());
loop.run();
System.exit(exitCode);
