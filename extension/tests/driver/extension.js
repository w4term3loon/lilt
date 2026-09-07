import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Shell from 'gi://Shell';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';

export default class Driver extends Extension {
    enable() {
        this.timers = new Set();
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
                try { connection.call_finish(result); }
                catch (error) { console.error(`LILT TEST FIXTURE ERROR ${error.message}`); }
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
        this.keyboard = Clutter.get_default_backend().get_default_seat()
            .create_virtual_device(Clutter.InputDeviceType.KEYBOARD_DEVICE);
        this.later(250, () => this.prepare());
        this.later(400, () => this.snapshot('before-start'));
        this.later(500, () => this.exercise());
    }

    exercise() {
        this.toggle();
        this.later(100, () => this.snapshot('start'));
        this.later(150, () => {
            this.key(Clutter.KEY_Shift_L, true);
            this.keyboard.notify_key(GLib.get_monotonic_time(), 30, Clutter.KeyState.PRESSED);
            this.key(Clutter.KEY_Shift_L, false);
            this.keyboard.notify_key(GLib.get_monotonic_time(), 30, Clutter.KeyState.RELEASED);
        });
        this.later(300, () => this.tap(Clutter.KEY_Return));
        this.later(420, () => this.tap(Clutter.KEY_F8));
        this.later(600, () => {
            console.log(`LILT TEST WRONG KEYS state=${this.lilt._state}`);
            console.log(`LILT TEST INLINE composition=${!!this.lilt._composition} grab=${!!this.lilt._grab}`);
            this.snapshot('draft');
            this.capture('recording');
            this.recordingScale = this.lilt._bars[4].scale_y;
        });
        this.later(850, () => {
            this.snapshot('revised');
            console.log(`LILT TEST LEVEL changed=${Math.abs(this.recordingScale - this.lilt._bars[4].scale_y) > 0.02}`);
        });
        this.later(900, () => this.key(Clutter.KEY_Control_L, true));
        this.later(1000, () => this.key(Clutter.KEY_F8, true));
        this.later(1150, () => this.capture('transcribing'));
        this.later(1700, () => { this.logHeld('HELD'); this.snapshot('held'); });
        this.later(1900, () => this.key(Clutter.KEY_F8, false));
        this.later(2050, () => { this.logHeld('MODIFIER'); this.snapshot('modifier'); });
        this.later(2200, () => this.key(Clutter.KEY_Control_L, false));
        this.later(2600, () => { this.logFinished('FINISHED'); this.snapshot('final'); });

        this.later(2800, () => this.prepare());
        this.later(3000, () => this.toggle());
        this.later(3300, () => this.toggleDown());
        this.later(3900, () => { this.logHeld('TOGGLE HELD'); this.snapshot('toggle-held'); });
        this.later(4100, () => this.toggleUp());
        this.later(4500, () => { this.logFinished('TOGGLE FINISHED'); this.snapshot('toggle-final'); });

        this.later(4700, () => this.prepare());
        this.later(4900, () => this.toggle());
        this.later(5300, () => this.key(Clutter.KEY_Escape, true));
        this.later(5650, () => this.key(Clutter.KEY_Escape, false));
        this.later(6000, () => { this.logFinished('CANCELED'); this.snapshot('cancel'); });

        this.later(6200, () => {
            this.prepare();
            this.fixture('FinishShortcut', '(s)', ['Return']);
        });
        this.later(6400, () => this.toggle());
        this.later(6800, () => this.key(Clutter.KEY_Return, true));
        this.later(7900, () => this.snapshot('enter-held'));
        this.later(8200, () => this.key(Clutter.KEY_Return, false));
        this.later(8500, () => this.snapshot('enter-final'));

        this.later(8800, () => this.prepare());
        this.later(9000, () => this.toggle());
        this.later(9400, () => this.focus('lilt other entry'));
        this.later(9800, () => this.snapshot('focus-lost'));

        this.later(10100, () => this.focus('lilt test entry'));
        this.later(10300, () => this.prepare());
        this.later(10500, () => this.toggle());
        this.later(10900, () => this.fixture('SwitchEngine', '(s)', ['xkb:gb::eng']));
        this.later(11400, () => this.snapshot('engine-change'));
        this.later(11600, () => this.fixture('SwitchEngine', '(s)', ['xkb:us::eng']));
        this.later(11900, () => this.prepare());
        this.later(12100, () => this.toggle());
        this.later(12500, () => this.fixture('Close'));
        this.later(13000, () => this.snapshot('target-closed'));
    }

    logHeld(label) {
        console.log(`LILT TEST ${label} composition=${!!this.lilt._composition} session=${this.lilt._session} keys=${this.lilt._heldKeys.size}`);
    }

    logFinished(label) {
        console.log(`LILT TEST ${label} grab=${!!this.lilt._grab} session=${this.lilt._session} inserting=${this.lilt._inserting} pulse=${!!this.lilt._pulseSource}`);
    }

    async capture(label) {
        try {
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
        this.keyboard?.run_dispose();
    }
}
