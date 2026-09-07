// Run with: gjs extension/tests/test-lifecycle.js
// Deliver D-Bus callbacks in adversarial order without touching the desktop.
const GLib = imports.gi.GLib;
const IBus = imports.gi.IBus;
const System = imports.system;
const path = GLib.build_filenamev([
    GLib.path_get_dirname(System.programInvocationName), '..', 'extension.js',
]);
const source = imports.byteArray.toString(GLib.file_get_contents(path)[1])
    .replace(/^import .*;$/gm, '')
    .replace('export default class LiltExtension extends Extension',
        'globalThis.TestExtension = class LiltExtension extends Extension');
const Clutter = {ModifierType: IBus.ModifierType};
for (const name of Object.keys(IBus).filter(key => key.startsWith('KEY_')))
    Clutter[name] = IBus[name];
const proxies = [];
class FakeProxy {
    constructor(_bus, _name, _path, initialized, cancellable) {
        this.initialized = error => initialized(this, error);
        this.cancellable = cancellable;
        this.g_name_owner = ':1.1';
        this.signals = new Set();
        this.calls = [];
        this.finished = 0;
        proxies.push(this);
    }

    connect(name) { this.signals.add(name); return name; }
    connectSignal(name) { return this.connect(name); }
    disconnect(name) { this.signals.delete(name); }
    disconnectSignal(name) { this.disconnect(name); }
    call(method, _parameters, _flags, _timeout, _cancellable, callback) {
        this.calls.push({method, complete: error => callback?.(this, error)});
    }

    call_finish(error) {
        this.finished++;
        if (error)
            throw error;
    }
}
const Gio = {
    Cancellable: imports.gi.Gio.Cancellable,
    DBus: {session: {}},
    DBusCallFlags: {NONE: 0, NO_AUTO_START: 1},
    DBusProxyFlags: {DO_NOT_AUTO_START_AT_CONSTRUCTION: 1},
    DBusProxy: {makeProxyWrapper: () => FakeProxy},
};
const Extension = class {};
const Main = {
    layoutManager: {connect() { return 1; }, disconnect() {}, removeChrome() {}},
    wm: {removeKeybinding() {}},
};
const IBusManager = {
    getIBusManager: () => ({connect() { return 1; }, disconnect() {}}),
};
eval(source);

function assert(condition, message) {
    if (!condition)
        throw new Error(message);
}

function fixture() {
    const instance = new TestExtension();
    Object.assign(instance, {
        errors: [], owners: 0, draws: 0,
        getSettings: () => ({connect() { return 1; }, disconnect() {}}),
        _makeUi() { this._pill = {destroy() {}}; this._indicator = {destroy() {}}; },
        _bindShortcut() {}, _watchIbusPanel() {}, _disconnectIbusPanel() {},
        _stopWave() {}, _releaseGrab() {}, _clearTarget() {}, _finishSession() {},
        _drawState() { this.draws++; },
        _ownerChanged() { this.owners++; },
        _error(message) { this.errors.push(message); },
    });
    return instance;
}

for (const error of [null, new Error('Obsolete initialization failed')]) {
    const instance = fixture();
    instance.enable();
    const oldProxy = proxies.at(-1);
    instance.disable();
    assert(oldProxy.cancellable.is_cancelled(), 'Disable must cancel pending proxy initialization');
    instance.enable();
    const currentProxy = proxies.at(-1);
    oldProxy.initialized(error);
    assert(oldProxy.signals.size === 0 && !instance._proxyReady && instance.owners === 0 &&
        instance.errors.length === 0, 'An old initialization must not affect the next enable');
    currentProxy.initialized(null);
    assert(currentProxy.signals.size === 4 && instance._proxyReady && instance.owners === 1,
        'The current initialization must connect exactly its own handlers');
    instance.disable();
    assert(currentProxy.signals.size === 0 && oldProxy.signals.size === 0,
        'Disable must leave no handlers on either proxy');
}

for (const error of [null, new Error('Obsolete method failed')]) {
    const instance = fixture();
    instance.enable();
    const oldProxy = proxies.at(-1);
    oldProxy.initialized(null);
    let completed = 0;
    instance._call('Attach', null, () => completed++);
    const oldCall = oldProxy.calls.at(-1);
    instance.disable();
    instance.enable();
    const currentProxy = proxies.at(-1);
    currentProxy.initialized(null);
    oldCall.complete(error);
    assert(oldProxy.finished === 1, 'Even obsolete asynchronous results must be finished');
    assert(completed === 0 && instance.errors.length === 0 && instance.draws === 0 &&
        instance._state === 'idle', 'An old method completion must not mutate the next enable');
    instance._call('Attach', null, () => completed++);
    currentProxy.calls.at(-1).complete(null);
    assert(completed === 1, 'A current method completion must run normally');
    instance._call('Stop', null, () => completed++);
    const oldSessionCall = currentProxy.calls.at(-1);
    instance._generation++;
    oldSessionCall.complete(error);
    assert(completed === 1 && instance.errors.length === 0,
        'An obsolete session completion must not affect a newer recording');
    instance.disable();
}
print('D-Bus disable/re-enable initialization, signal cleanup, stale result, and session-generation regressions passed');

class FakePanel {
    constructor() { this.signals = new Map(); this.disposed = false; }
    connect(name, callback) { this.signals.set(name, callback); return name; }
    disconnect(name) {
        assert(!this.disposed, 'Never disconnect a handler from a disposed IBus panel');
        this.signals.delete(name);
    }

    destroy() {
        this.signals.get('destroy')?.();
        this.signals.clear();
        this.disposed = true;
    }
}
const panelInstance = new TestExtension();
const previousPanel = new FakePanel();
panelInstance._ibus = {_panelService: previousPanel};
panelInstance._watchIbusPanel();
previousPanel.signals.get('focus-in')(previousPanel, '/old/context');
assert(panelInstance._ibusContext === '/old/context', 'Track the current IBus input context');
previousPanel.destroy();
assert(panelInstance._ibusPanel === null && panelInstance._ibusContext === null,
    'A destroyed IBus panel must immediately release its reference and focus context');
const nextPanel = new FakePanel();
panelInstance._ibus._panelService = nextPanel;
panelInstance._watchIbusPanel();
assert(nextPanel.signals.size === 3, 'Watch the replacement IBus panel after reconnection');
panelInstance._disconnectIbusPanel();
assert(nextPanel.signals.size === 0, 'Disable must disconnect all surviving IBus panel handlers');
print('IBus panel destruction, reconnection, and handler cleanup regressions passed');
