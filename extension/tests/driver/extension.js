import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Shell from 'gi://Shell';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';

export default class Driver extends Extension {
    enable() {
        this.timers = new Set();
        this.frames = {};
        this.recordings = 0;
        // GTK must discover a keyboard-capable Wayland seat when it connects.
        // A headless CI compositor can have no physical input devices at all.
        this.keyboard = Clutter.get_default_backend().get_default_seat()
            .create_virtual_device(Clutter.InputDeviceType.KEYBOARD_DEVICE);
        GLib.file_set_contents(GLib.build_filenamev([GLib.getenv('LILT_SMOKE_ROOT'), 'display.json']),
            JSON.stringify({DISPLAY: GLib.getenv('DISPLAY'), XAUTHORITY: GLib.getenv('XAUTHORITY')}));
        this.later(3500, () => this.run());
    }

    later(ms, fn) {
        const id = GLib.timeout_add(GLib.PRIORITY_DEFAULT, ms, () => {
            this.timers.delete(id);
            fn();
            return GLib.SOURCE_REMOVE;
        });
        this.timers.add(id);
    }

    fixture(method, signature = '', args = []) {
        Gio.DBus.session.call('io.github.lilt.Dictation', '/io/github/lilt/Dictation',
            'io.github.lilt.TestEntry', method,
            signature ? new GLib.Variant(signature, args) : null, null,
            Gio.DBusCallFlags.NONE, 3000, null, (connection, result) => {
                try {
                    connection.call_finish(result);
                    if (method === 'Snapshot' && args[0] === 'target-closed')
                        GLib.file_set_contents(GLib.build_filenamev([
                            GLib.getenv('LILT_SMOKE_ROOT'), 'complete']), 'true');
                } catch (error) {
                    console.error(`LILT TEST FIXTURE ERROR ${error.message}`);
                }
            });
    }

    snapshot(name) { this.fixture('Snapshot', '(s)', [name]); }
    prepare() { this.fixture('Prepare', '(sii)', ['before REPLACE after', 7, 14]); }

    focus(title) {
        const target = global.get_window_actors().map(actor => actor.meta_window)
            .find(window => window.title === title);
        if (target)
            Main.activateWindow(target);
        return Boolean(target);
    }

    run() {
        this.lilt = Main.extensionManager.lookup(GLib.getenv('LILT_EXTENSION_UUID')).stateObj;
        if (!this.focus('lilt test entry')) {
            this.later(500, () => this.run());
            return;
        }
        Main.overview.hide();
        this.keyCounts = {press: 0, release: 0, consumed: 0, sample: []};
        this.originalCapture = this.lilt._capture;
        this.captureProbe = event => {
            const type = event.type();
            const result = this.originalCapture.call(this.lilt, event);
            if (type === Clutter.EventType.KEY_PRESS || type === Clutter.EventType.KEY_RELEASE) {
                const direction = type === Clutter.EventType.KEY_PRESS ? 'press' : 'release';
                this.keyCounts[direction]++;
                this.keyCounts.consumed += Number(result === Clutter.EVENT_STOP);
                if (this.keyCounts.sample.length < 6)
                    this.keyCounts.sample.push([direction, event.get_key_symbol()]);
            }
            return result;
        };
        this.lilt._capture = this.captureProbe;
        this.later(250, () => this.prepare());
        this.later(400, () => {
            this.logRoute('before-start');
            this.snapshot('before-start');
        });
        this.later(500, () => this.exercise().catch(error => {
            console.error(`LILT TEST FIXTURE ERROR ${error.message}`);
        }));
    }

    async startRecording() {
        this.toggle();
        const deadline = GLib.get_monotonic_time() + 5 * 1000000;
        while (this.lilt._state !== 'recording' || this.lilt._preparing ||
            !this.lilt._latestPartial?.text) {
            if (GLib.get_monotonic_time() >= deadline) {
                this.logRoute('readiness-timeout');
                throw new Error(`Recording did not become ready: state=${this.lilt._state}, preparing=${this.lilt._preparing}, composition=${!!this.lilt._composition}, draft=${!!this.lilt._latestPartial?.text}`);
            }
            await this.pause(25);
        }
        if (++this.recordings === 1)
            this.logRoute('recording');
    }

    pause(ms) {
        return new Promise(resolve => this.later(ms, resolve));
    }

    async exercise() {
        const steps = [];
        const step = (at, action) => steps.push([at, action]);
        await this.startRecording();
        step(100, () => this.snapshot('start'));
        step(150, () => {
            this.key(Clutter.KEY_Shift_L, true);
            this.keyboard.notify_key(GLib.get_monotonic_time(), 30, Clutter.KeyState.PRESSED);
            this.key(Clutter.KEY_Shift_L, false);
            this.keyboard.notify_key(GLib.get_monotonic_time(), 30, Clutter.KeyState.RELEASED);
        });
        step(300, () => this.tap(Clutter.KEY_Return));
        step(420, () => this.tap(Clutter.KEY_F8));
        step(600, () => {
            console.log(`LILT TEST WRONG KEYS state=${this.lilt._state}`);
            console.log(`LILT TEST INLINE composition=${!!this.lilt._composition} grab=${!!this.lilt._grab}`);
            this.logRoute('draft');
            this.snapshot('draft');
            this.capture('recording');
            this.recordingHeight = this.lilt._bars[4].height;
            const rounded = this.lilt._bars.every(bar =>
                bar.scale_y === 1 && bar.height >= bar.width && bar.height <= 23);
            console.log(`LILT TEST CAPS unscaled=${rounded}`);
        });
        step(850, () => {
            this.snapshot('revised');
            console.log(`LILT TEST LEVEL changed=${Math.abs(this.recordingHeight - this.lilt._bars[4].height) > 0.4}`);
        });
        step(900, () => this.key(Clutter.KEY_Control_L, true));
        step(1000, () => this.key(Clutter.KEY_F8, true));
        step(1050, () => { this.dotPositions = this.lilt._dots.map(dot => dot.translation_y); });
        step(1150, () => {
            this.capture('transcribing');
            const changed = this.lilt._dots.some((dot, index) =>
                Math.abs(dot.translation_y - this.dotPositions[index]) > 0.2);
            console.log(`LILT TEST DOTS visible=${this.lilt._dotBox.visible} wave=${this.lilt._wave.visible} count=${this.lilt._dots.length} moving=${changed}`);
        });
        step(1700, () => { this.logHeld('HELD'); this.snapshot('held'); });
        step(1900, () => this.key(Clutter.KEY_F8, false));
        step(2050, () => { this.logHeld('MODIFIER'); this.snapshot('modifier'); });
        step(2200, () => this.key(Clutter.KEY_Control_L, false));
        step(2600, () => { this.logFinished('FINISHED'); this.snapshot('final'); });

        step(2800, () => this.prepare());
        step(3000, () => this.startRecording());
        step(3300, () => this.toggleDown());
        step(3900, () => { this.logHeld('TOGGLE HELD'); this.snapshot('toggle-held'); });
        step(4100, () => this.toggleUp());
        step(4500, () => { this.logFinished('TOGGLE FINISHED'); this.snapshot('toggle-final'); });

        step(4700, () => this.prepare());
        step(4900, () => this.startRecording());
        step(5300, () => this.key(Clutter.KEY_Escape, true));
        step(5650, () => this.key(Clutter.KEY_Escape, false));
        step(6000, () => { this.logFinished('CANCELED'); this.snapshot('cancel'); });

        step(6200, () => {
            this.prepare();
            this.fixture('FinishShortcut', '(s)', ['Return']);
        });
        step(6400, () => this.startRecording());
        step(6800, () => this.key(Clutter.KEY_Return, true));
        step(7900, () => this.snapshot('enter-held'));
        step(8200, () => this.key(Clutter.KEY_Return, false));
        step(8500, () => this.snapshot('enter-final'));

        step(8800, () => this.prepare());
        step(9000, () => this.startRecording());
        step(9400, () => this.focus('lilt other entry'));
        step(9800, () => this.snapshot('focus-lost'));

        step(10100, () => this.focus('lilt test entry'));
        step(10300, () => this.prepare());
        step(10500, () => this.startRecording());
        step(10900, () => this.fixture('SwitchEngine', '(s)', ['xkb:gb::eng']));
        step(11400, () => this.snapshot('engine-change'));
        step(11600, () => this.fixture('SwitchEngine', '(s)', ['xkb:us::eng']));
        step(11900, () => this.prepare());
        step(12100, () => this.startRecording());
        step(12500, () => this.fixture('Close'));
        step(13000, () => this.snapshot('target-closed'));

        // A cold IBus activation must not consume the timed key-test window.
        // Keep each scenario's original delays, pausing at every recording start.
        let previous = 0;
        for (const [at, action] of steps) {
            await this.pause(at - previous);
            await action();
            previous = at;
        }
    }

    logHeld(label) {
        console.log(`LILT TEST ${label} composition=${!!this.lilt._composition} session=${this.lilt._session} keys=${this.lilt._heldKeys.size}`);
    }

    logFinished(label) {
        console.log(`LILT TEST ${label} grab=${!!this.lilt._grab} session=${this.lilt._session} inserting=${this.lilt._inserting} dots=${!!this.lilt._dotSource} resting=${this.lilt._dots.every(dot => dot.translation_y === 0)}`);
    }

    logRoute(label) {
        // GNOME 46's inputMethod.js bypasses IBus without a context or current
        // source. Engine readiness alone cannot describe that Wayland bridge.
        const input = Main.inputMethod;
        const source = value => value ? `${value.type}:${value.id}` : null;
        const engine = value => value?.startsWith('lilt-dictation-') ? 'lilt-dictation' : value ?? null;
        console.log(`LILT TEST ROUTE ${label} ${JSON.stringify({
            context: input?._context?.get_object_path() ?? null,
            focus: !!input?.currentFocus,
            source: source(input?._currentSource),
            managerSource: source(input?._inputSourceManager.currentSource),
            preeditLength: input?._preeditStr?.length ?? 0,
            preeditVisible: !!input?._preeditVisible,
            busConnected: !!input?._ibus?.is_connected(),
            ibusReady: !!this.lilt._ibus?._ready,
            engine: engine(this.lilt._ibus?._currentEngineName),
        })}`);
        const session = this.lilt._composition?._session;
        if (session) {
            console.log(`LILT TEST SESSION ${label} ${JSON.stringify({
                state: this.lilt._state, preparing: this.lilt._preparing,
                original: session.originalContext, focused: session.focusedContext,
                capabilities: session.capabilities, ready: session.ready,
                closing: session.closing, cancelled: session.cancelled,
                previousEngine: engine(session.previousEngine),
            })}`);
            console.log(`LILT TEST KEYS ${label} ${JSON.stringify(this.keyCounts)}`);
        }
    }

    async capture(label) {
        try {
            const bounds = actor => {
                const [x, y] = actor.get_transformed_position();
                const [width, height] = actor.get_transformed_size();
                return {x, y, width, height};
            };
            this.frames[label] = {
                stageWidth: global.stage.width,
                bars: this.lilt._bars.map(bounds),
                dots: this.lilt._dots.map(bounds),
            };
            GLib.file_set_contents(GLib.build_filenamev([
                GLib.getenv('LILT_SMOKE_ROOT'), 'indicator-frames.json']),
                JSON.stringify(this.frames));
            const filename = GLib.build_filenamev([GLib.getenv('LILT_SMOKE_ROOT'), `${label}.png`]);
            const stream = Gio.File.new_for_path(filename).replace(null, false, Gio.FileCreateFlags.NONE, null);
            await new Shell.Screenshot().screenshot(false, stream);
            stream.close(null);
            console.log(`LILT TEST SCREENSHOT ${label} width=${this.lilt._pill.width} height=${this.lilt._pill.height}`);
        } catch (error) {
            console.error(`LILT TEST SCREENSHOT ERROR ${error.message}`);
        }
    }

    toggleDown() {
        this.key(Clutter.KEY_Control_L, true);
        this.key(Clutter.KEY_Super_L, true);
        this.key(Clutter.KEY_space, true);
    }
    toggleUp() {
        this.key(Clutter.KEY_space, false);
        this.key(Clutter.KEY_Super_L, false);
        this.key(Clutter.KEY_Control_L, false);
    }
    toggle() { this.toggleDown(); this.toggleUp(); }
    tap(key) { this.key(key, true); this.key(key, false); }
    key(key, pressed) {
        this.keyboard.notify_keyval(GLib.get_monotonic_time(), key,
            pressed ? Clutter.KeyState.PRESSED : Clutter.KeyState.RELEASED);
    }

    disable() {
        for (const timer of this.timers)
            GLib.source_remove(timer);
        this.timers.clear();
        if (this.captureProbe && this.lilt?._capture === this.captureProbe)
            this.lilt._capture = this.originalCapture;
        this.keyboard?.run_dispose();
    }
}
