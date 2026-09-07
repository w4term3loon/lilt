// Run with: gjs extension/tests/test-session.js
// Exercise event-order regressions without GNOME Shell, a keyboard grab, or D-Bus.
const GLib = imports.gi.GLib;
const System = imports.system;
imports.gi.versions.Gdk = '3.0';
const Gdk = imports.gi.Gdk;
const IBus = imports.gi.IBus;
const path = GLib.build_filenamev([
    GLib.path_get_dirname(System.programInvocationName), '..', 'extension.js',
]);
const source = imports.byteArray.toString(GLib.file_get_contents(path)[1])
    .replace(/^import .*;$/gm, '')
    .replace('export default class LiltExtension extends Extension',
        'globalThis.TestExtension = class LiltExtension extends Extension');
const textSource = imports.byteArray.toString(GLib.file_get_contents(
    GLib.build_filenamev([GLib.path_get_dirname(path), 'text.js']))[1]).replace(/^export /gm, '');
const Clutter = {
    EventType: {KEY_PRESS: 1, KEY_RELEASE: 2},
    ModifierType: Gdk.ModifierType,
    KEY_Return: 65293, KEY_KP_Enter: 65421, KEY_Escape: 65307,
    EVENT_STOP: 1, EVENT_PROPAGATE: 0,
};
for (const key of ['Shift_L', 'Shift_R', 'Control_L', 'Control_R', 'Alt_L', 'Alt_R',
    'Super_L', 'Super_R', 'Meta_L', 'Meta_R', 'Hyper_L', 'Hyper_R',
    'Caps_Lock', 'Shift_Lock', 'Num_Lock', 'Mode_switch', 'ISO_Level3_Shift', 'ISO_Left_Tab', 'Tab'])
    Clutter[`KEY_${key}`] = Gdk[`KEY_${key}`];
const Gio = {DBusProxy: {makeProxyWrapper: () => function () {}}};
const Extension = class {};
const Main = {};
const global = {get_pointer: () => [0, 0, 0]};
eval(textSource + '\n' + source + '\nglobalThis.parseShortcutForTest = parseShortcut;');

function assert(condition, message) {
    if (!condition)
        throw new Error(message);
}

function fixture() {
    const instance = new TestExtension();
    Object.assign(instance, {
        _heldKeys: new Set(), _sources: new Set(), _pendingText: null,
        _state: 'recording', _session: true, _generation: 1, _cancelled: false,
        _finishScheduled: false, _finishWaiting: false, _sessionLive: false,
        _sessionFinish: parseShortcutForTest('Return'),
        _sessionStart: parseShortcutForTest('<Primary><Super>space'),
        _pill: {hide() {}}, callbacks: [], calls: [], released: false, cleared: false,
        _target: {},
        drafts: [], retryCallbacks: [],
    });
    instance._composition = {update(text, stableBytes) { instance.drafts.push({text, stableBytes}); }};
    instance._call = method => instance.calls.push(method);
    instance._clearTarget = () => { instance.cleared = true; };
    instance._later = (delay, fn) => (delay === 30 ? instance.retryCallbacks : instance.callbacks).push(fn);
    instance._releaseGrab = () => { instance.released = true; };
    return instance;
}

const event = (type, symbol, keycode, state = 0) => ({
    type: () => type,
    get_key_symbol: () => symbol,
    get_key_code: () => keycode,
    get_state: () => state,
});

let instance = fixture();
instance._capture(event(1, 65, 38)); // Shift+A keydown
instance._capture(event(2, 97, 38)); // A keyup after Shift has been released
assert(instance._heldKeys.size === 0, 'Modifier changes must not strand held keys');

instance = fixture();
assert(instance._capture(event(1, Clutter.KEY_Return, 36)) === Clutter.EVENT_STOP,
    'Return keydown must be consumed');
assert(instance._capture(event(1, Clutter.KEY_Return, 36)) === Clutter.EVENT_STOP,
    'Return autorepeat must be consumed');
assert(instance.calls.filter(method => method === 'Stop').length === 1,
    'Autorepeat must not repeatedly stop recording');
assert(instance._capture(event(2, Clutter.KEY_Return, 36)) === Clutter.EVENT_STOP,
    'Return keyup must be consumed');

instance = fixture();
instance._capture(event(1, Clutter.KEY_Return, 36));
instance._finishSession();
assert(instance.callbacks.length === 0 && !instance.released,
    'A held finish key must retain the grab');
instance._capture(event(2, Clutter.KEY_Return, 36));
assert(instance.callbacks.length === 1, 'Finish must resume after key release');
instance.callbacks.shift()();
assert(instance.released, 'Finished session must release the grab after keyup');

instance = fixture();
instance._finishSession();
instance._capture(event(1, Clutter.KEY_Return, 36));
instance.callbacks.shift()();
assert(!instance.released, 'A new Return before scheduled cleanup must retain the grab');
instance._capture(event(2, Clutter.KEY_Return, 36));
instance.callbacks.shift()();
assert(instance.released, 'Scheduled cleanup must resume after the new Return releases');

instance = fixture();
instance._capture(event(1, Clutter.KEY_Escape, 9));
instance._receiveTranscript('Transcript already in transit');
assert(instance._pendingText === null, 'A late transcript must not undo cancellation');

instance = fixture();
instance._insert('Old transcript', 0, 0);
assert(!instance.cleared && instance.callbacks.length === 0,
    'An obsolete insertion callback must not touch the new session');

instance = fixture();
instance._cancelled = true;
instance._insert('Cancelled transcript', 0, instance._generation);
assert(instance.cleared && instance.callbacks.length === 0,
    'Cancellation must discard an insertion waiting for focus restoration');

instance = fixture();
Object.assign(instance, {
    _enabled: true, _session: false, _inserting: true,
    _proxy: {g_name_owner: ':1.1', State: 'transcribing', Message: ''},
    _drawState() {},
    _beginSession() { this.calls.push('BeginSession'); return false; },
});
instance._sync();
assert(instance.calls.length === 0,
    'A late transcribing property update must not cancel insertion or start a new session');

// Finish shortcut matching must use the configured keysym and exact modifiers.
const modifiers = Clutter.ModifierType;
instance = fixture();
instance._sessionFinish = parseShortcutForTest('<Primary><Shift>f');
for (const [key, state] of [
    [Clutter.KEY_Return, 0],
    [Gdk.KEY_f, 0],
    [Gdk.KEY_f, modifiers.CONTROL_MASK],
    [Gdk.KEY_F, modifiers.CONTROL_MASK | modifiers.SHIFT_MASK | modifiers.MOD1_MASK],
]) {
    assert(instance._capture(event(1, key, 41, state)) === Clutter.EVENT_STOP,
        'A non-finish key must still be consumed');
    assert(instance._capture(event(2, key, 41, state)) === Clutter.EVENT_STOP,
        'A non-finish release must still be consumed');
}
assert(instance.calls.length === 0, 'Enter, missing modifiers, and extra modifiers must not finish');
instance._capture(event(1, Gdk.KEY_F, 41,
    modifiers.CONTROL_MASK | modifiers.SHIFT_MASK | modifiers.LOCK_MASK | modifiers.MOD2_MASK));
assert(instance.calls.join() === 'Stop', 'Matching must ignore Caps/NumLock and normalize letter case');

for (const [binding, key, state] of [
    ['<Super>F8', Gdk.KEY_F8, modifiers.MOD4_MASK],
    ['F8', Gdk.KEY_F8, 0],
    ['z', Gdk.KEY_z, 0],
    ['Return', Clutter.KEY_KP_Enter, 0],
    ['<Shift>Tab', Clutter.KEY_ISO_Left_Tab, modifiers.SHIFT_MASK],
]) {
    instance = fixture();
    instance._sessionFinish = parseShortcutForTest(binding);
    instance._capture(event(1, key, 41, state));
    assert(instance.calls.join() === 'Stop', `Match the normalized finish binding: ${binding}`);
}

instance = fixture();
instance._sessionFinish = parseShortcutForTest('<Control>F8');
instance._capture(event(1, Clutter.KEY_Control_L, 37));
assert(instance._capture(event(1, Gdk.KEY_F8, 74, modifiers.CONTROL_MASK)) === Clutter.EVENT_STOP,
    'A custom finish press must be consumed');
instance._finishSession();
instance._capture(event(2, Gdk.KEY_F8, 74, modifiers.CONTROL_MASK));
assert(!instance.released && instance.callbacks.length === 0,
    'Releasing the finish key must retain the grab while its modifier is held');
assert(instance._capture(event(2, Clutter.KEY_Control_L, 37)) === Clutter.EVENT_STOP,
    'The finish modifier release must be consumed');
instance.callbacks.shift()();
assert(instance.released, 'The final modifier release must finish cleanup');

instance = fixture();
instance._sessionFinish = parseShortcutForTest('<Primary><Super>space');
instance._capture(event(1, Gdk.KEY_space, 65, modifiers.CONTROL_MASK | modifiers.MOD4_MASK));
assert(instance.calls.join() === 'Stop', 'Identical start and finish bindings must stop only once');

instance = fixture();
Object.assign(instance, {
    _enabled: true,
    _proxy: {g_name_owner: ':1.1', State: 'recording', FinishShortcut: 'F8'},
    _drawState() {},
});
instance._sync();
instance._capture(event(1, Clutter.KEY_Return, 36));
assert(instance._finishShortcut === 'F8' && instance.calls.join() === 'Stop',
    'A setting update must affect the next session while preserving the current finish binding');

for (const invalid of ['', 'made_up_key', '<NoSuchModifier>a', 'Control_L', 'Shift_L',
    'Escape', '<Control>Escape', 'Caps_Lock', 'Shift_Lock', 'Num_Lock', 'Mode_switch',
    'XF86Return', 'XF86made_up_key']) {
    assert(parseShortcutForTest(invalid) === null, `Reject invalid finish binding: ${invalid}`);
}

// Representative GTK preferences names cover key classes and non-ASCII case
// folding, without retesting every constant exposed by the upstream library.
for (const name of ['a', 'Z', 'Aacute', 'adiaeresis', 'Greek_ALPHA', 'Cyrillic_YA',
    'F8', 'space', 'Tab', 'Return', 'AudioMute', 'XF86AudioMute']) {
    const key = Gdk.keyval_from_name(name);
    assert(key !== Gdk.KEY_VoidSymbol && parseShortcutForTest(name)?.key === Gdk.keyval_to_lower(key),
        `Preserve GTK shortcut names and case folding: ${name}`);
}

instance = fixture();
instance._sessionLive = true;
instance._receivePartial('Draft', 0);
instance._capture(event(1, Clutter.KEY_Escape, 9));
assert(instance.drafts.at(-1).text === '', 'Escape must immediately clear composition');
const count = instance.drafts.length;
instance._receivePartial('Late canceled draft', 0);
assert(instance.drafts.length === count && instance._pendingText === null,
    'Late partial updates must not undo cancellation');

for (const overrides of [{_session: false}, {_target: null}, {_state: 'idle'},
    {_pendingText: 'Final'}, {_sessionLive: false}]) {
    instance = fixture();
    Object.assign(instance, {_sessionLive: true}, overrides);
    instance._receivePartial('Stale draft', 5);
    assert(instance.drafts.length === 0, 'Ignore partials outside an active live session or after final output');
}
instance = fixture();
instance._sessionLive = true;
instance._receivePartial('Draft', 0);
instance._receiveTranscript('Final text');
assert(instance.drafts.at(-1).text === '' && instance._pendingText === 'Final text',
    'Final transcript clears composition and waits for the safe final commit');
instance._receivePartial('Out of order draft', 0);
assert(instance.drafts.at(-1).text === '', 'Final output must not be replaced by a late partial');

instance = fixture();
instance._sessionLive = true;
assert(instance._capture(event(2, Clutter.KEY_Control_L, 37)) === Clutter.EVENT_PROPAGATE,
    'A modifier pressed before composition must receive its matching release');

// The daemon's pre-Start idle reply may arrive after input-method preparation
// begins. It must not tear down the destination before Toggle becomes active.
for (const phase of ['_preparing', '_awaitingNativeStart']) {
    instance = fixture();
    Object.assign(instance, {
        _enabled: true, _state: 'loading', [phase]: true,
        _proxy: {g_name_owner: ':1.1', State: 'idle', Message: '', FinishShortcut: 'Return'},
        _drawState() { this.calls.push('Draw'); },
    });
    instance._sync();
    assert(instance._state === 'loading' && instance._session &&
        instance.callbacks.length === 0 && instance.calls.length === 0,
    'An old idle reply must not finish a pending Start');
}
instance = fixture();
instance._awaitingNativeStart = true;
instance._finishSession();
assert(instance.callbacks.length === 0 && !instance.released,
    'Finish must wait until the native Start acknowledgment or cancellation');
instance = fixture();
instance._cancelled = true;
instance._cancelFromFocus();
assert(instance.calls.length === 0,
    'A second focus-out notification must not duplicate Cancel after cancellation');
print('Session event-order, shortcut semantics, stale output, cancellation, and held-key regressions passed');
