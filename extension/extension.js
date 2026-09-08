// SPDX-License-Identifier: GPL-3.0-or-later
import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import IBus from 'gi://IBus';
import Meta from 'gi://Meta';
import Shell from 'gi://Shell';
import St from 'gi://St';

import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PanelMenu from 'resource:///org/gnome/shell/ui/panelMenu.js';
import * as PopupMenu from 'resource:///org/gnome/shell/ui/popupMenu.js';
import * as IBusManager from 'resource:///org/gnome/shell/misc/ibusManager.js';
import {insertionText, isBrowserCommand, isBrowserCommandPreview, startsBrowserCommand} from './text.js';
import {Composition} from './composition.js';
import {drawOrb, sampleOrb, voiceIntensity} from './orb.js';

const BUS_NAME = 'io.github.ren.Dictation';
const BUS_PATH = '/io/github/ren/Dictation';
const BUS_XML = `<node><interface name="${BUS_NAME}">
    <method name="Attach"/><method name="Detach"/>
    <method name="Toggle"/><method name="Stop"/><method name="Cancel"/>
    <method name="ShowPreferences"/><method name="Quit"/>
    <method name="ReportError"><arg type="s" direction="in" name="message"/></method>
    <property name="State" type="s" access="read"/>
    <property name="Level" type="d" access="read"/>
    <property name="Bands" type="ad" access="read"/>
    <property name="Message" type="s" access="read"/>
    <property name="Shortcut" type="s" access="read"/>
    <property name="FinishShortcut" type="s" access="read"/>
    <property name="LivePreview" type="b" access="read"/>
    <signal name="Transcript"><arg type="s" name="text"/></signal>
    <signal name="PartialTranscript"><arg type="s" name="text"/><arg type="u" name="stable_bytes"/></signal>
</interface></node>`;
const DictationProxy = Gio.DBusProxy.makeProxyWrapper(BUS_XML);
const ACTIVE = new Set(['loading', 'recording', 'transcribing']);
const MODS = Clutter.ModifierType;
const SHORTCUT_MODIFIERS = MODS.SHIFT_MASK | MODS.CONTROL_MASK |
    MODS.MOD1_MASK | MODS.SUPER_MASK;
const MODIFIER_NAMES = new Map([
    ['shift', MODS.SHIFT_MASK], ['control', MODS.CONTROL_MASK],
    ['ctrl', MODS.CONTROL_MASK], ['primary', MODS.CONTROL_MASK],
    ['alt', MODS.MOD1_MASK], ['mod1', MODS.MOD1_MASK],
    ['super', MODS.SUPER_MASK], ['mod4', MODS.SUPER_MASK],
]);
const MODIFIER_KEYS = new Set([
    Clutter.KEY_Shift_L, Clutter.KEY_Shift_R,
    Clutter.KEY_Control_L, Clutter.KEY_Control_R,
    Clutter.KEY_Alt_L, Clutter.KEY_Alt_R,
    Clutter.KEY_Super_L, Clutter.KEY_Super_R,
    Clutter.KEY_Meta_L, Clutter.KEY_Meta_R,
    Clutter.KEY_Hyper_L, Clutter.KEY_Hyper_R,
    Clutter.KEY_Caps_Lock, Clutter.KEY_Shift_Lock, Clutter.KEY_Num_Lock,
    Clutter.KEY_ISO_Level3_Shift, Clutter.KEY_Mode_switch,
]);

function shortcutModifiers(state) {
    // Mutter reports Super both as the virtual SUPER and physical MOD4 bit.
    if (state & MODS.MOD4_MASK)
        state |= MODS.SUPER_MASK;
    return state & SHORTCUT_MODIFIERS;
}

function shortcutKey(key) {
    if (key === Clutter.KEY_ISO_Left_Tab)
        return Clutter.KEY_Tab;
    if (key === Clutter.KEY_KP_Enter)
        return Clutter.KEY_Return;
    return IBus.keyval_to_lower(key);
}

function parseShortcut(accelerator) {
    // Meta's accelerator parser is private in Mutter 46. IBus exposes the same
    // keysym names and case conversion without loading GTK/GDK into Shell.
    let rest = accelerator ?? '';
    let modifiers = 0;
    while (rest.startsWith('<')) {
        const token = /^<([^>]+)>/.exec(rest);
        const modifier = token && MODIFIER_NAMES.get(token[1].toLowerCase());
        if (!modifier)
            return null;
        modifiers |= modifier;
        rest = rest.slice(token[0].length);
    }
    // GTK also accepts the X11 XF86 prefix for multimedia key names.
    let key = IBus.keyval_from_name(rest);
    if (key === IBus.KEY_VoidSymbol && rest.startsWith('XF86')) {
        key = IBus.keyval_from_name(rest.slice(4));
        if ((key & 0xffff0000) !== 0x10080000)
            return null;
    }
    if (!key || key === IBus.KEY_VoidSymbol || MODIFIER_KEYS.has(key) ||
        key === Clutter.KEY_Escape)
        return null;
    if (key === Clutter.KEY_ISO_Left_Tab)
        modifiers |= MODS.SHIFT_MASK;
    return {key: shortcutKey(key), modifiers};
}

function matchesShortcut(binding, event) {
    return binding && shortcutKey(event.get_key_symbol()) === binding.key &&
        shortcutModifiers(event.get_state()) === binding.modifiers;
}

export default class RenExtension extends Extension {
    enable() {
        this._enabled = true;
        const enableGeneration = this._enableGeneration = (this._enableGeneration ?? 0) + 1;
        this._generation = (this._generation ?? 0) + 1;
        this._cancellable = new Gio.Cancellable();
        this._attached = false;
        this._proxyReady = false;
        this._state = 'idle';
        this._finishShortcut = 'Return';
        this._preparing = false;
        this._awaitingNativeStart = false;
        this._finishScheduled = false;
        this._finishWaiting = false;
        this._releaseRetry = false;
        this._sources = new Set();
        this._heldKeys = new Set();
        this._settings = this.getSettings();
        this._motionSettings = new Gio.Settings({schema_id: 'org.gnome.desktop.interface'});
        this._motionSignal = this._motionSettings.connect('changed::enable-animations', () => {
            this._stopOrb();
            this._drawState();
        });
        this._makeUi();
        this._bindShortcut();
        this._settingsSignal = this._settings.connect('changed::toggle-shortcut',
            () => this._bindShortcut());
        this._monitorSignal = Main.layoutManager.connect('monitors-changed',
            () => this._positionPill());

        this._ibus = IBusManager.getIBusManager();
        this._ibusReadySignal = this._ibus.connect('ready', () => this._watchIbusPanel());
        this._watchIbusPanel();

        // Stay idle at login, but allow the first action (and actions after Quit)
        // to activate the service. DO_NOT_AUTO_START would also block those calls.
        this._proxy = new DictationProxy(Gio.DBus.session, BUS_NAME, BUS_PATH,
            (proxy, error) => {
                if (!this._enabled || enableGeneration !== this._enableGeneration)
                    return;
                if (error) {
                    this._error(`Could not connect to ren: ${error.message}`);
                    return;
                }
                this._proxyReady = true;
                this._proxyPropertySignal = proxy.connect('g-properties-changed',
                    () => this._sync());
                this._proxyOwnerSignal = proxy.connect('notify::g-name-owner',
                    () => this._ownerChanged());
                this._transcriptSignal = proxy.connectSignal('Transcript',
                    (_proxy, _sender, [text]) => this._receiveTranscript(text));
                this._partialSignal = proxy.connectSignal('PartialTranscript',
                    (_proxy, _sender, [text, stableBytes]) => this._receivePartial(text, stableBytes));
                this._ownerChanged();
            }, this._cancellable, Gio.DBusProxyFlags.DO_NOT_AUTO_START_AT_CONSTRUCTION);
    }

    disable() {
        this._enabled = false;
        this._feedback = '';
        this._feedbackFrame = null;
        this._enableGeneration++;
        this._generation++;
        this._cancellable.cancel();
        this._cancellable = null;
        this._attached = false;
        this._cancelled = true;
        // Detach cancels any active native session. Do not reactivate a stopped service.
        if (this._proxyReady && this._proxy.g_name_owner)
            this._proxy.call('Detach', null, Gio.DBusCallFlags.NO_AUTO_START, 1000, null, null);
        this._stopWave();
        for (const source of this._sources)
            GLib.source_remove(source);
        this._sources.clear();
        this._releaseGrab();
        this._clearTarget();
        this._disconnectIbusPanel();
        this._ibus.disconnect(this._ibusReadySignal);
        this._ibus = null;
        if (this._transcriptSignal)
            this._proxy.disconnectSignal(this._transcriptSignal);
        if (this._partialSignal)
            this._proxy.disconnectSignal(this._partialSignal);
        for (const signal of [this._proxyPropertySignal, this._proxyOwnerSignal]) {
            if (signal)
                this._proxy.disconnect(signal);
        }
        this._proxy = null;
        this._proxyReady = false;
        this._transcriptSignal = 0;
        this._partialSignal = 0;
        this._proxyPropertySignal = 0;
        this._proxyOwnerSignal = 0;
        this._motionSettings.disconnect(this._motionSignal);
        this._motionSettings = null;
        this._settings.disconnect(this._settingsSignal);
        Main.wm.removeKeybinding('toggle-shortcut');
        this._settings = null;
        Main.layoutManager.disconnect(this._monitorSignal);
        Main.layoutManager.removeChrome(this._pill);
        this._pill.destroy();
        this._pill = null;
        this._wave = null;
        this._dots = null;
        this._dotBox = null;
        this._indicator.destroy();
        this._indicator = null;
        this._session = false;
        this._inserting = false;
        this._pendingText = null;
        this._finishWaiting = false;
        this._finishScheduled = false;
        this._preparing = false;
        this._awaitingNativeStart = false;
        this._releaseRetry = false;
    }

    _makeUi() {
        this._indicator = new PanelMenu.Button(0.0, 'Ren');
        this._panelIcon = new St.Icon({
            gicon: Gio.icon_new_for_string(`${this.path}/wren-symbolic.svg`),
            style_class: 'system-status-icon',
        });
        this._indicator.add_child(this._panelIcon);
        this._recordItem = this._indicator.menu.addAction('Start dictation', () => this._toggle());
        this._indicator.menu.addAction('Preferences', () => this._call('ShowPreferences'));
        this._indicator.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
        this._indicator.menu.addAction('Quit', () => this._call('Quit'));
        Main.panel.addToStatusArea(this.uuid, this._indicator);

        this._pill = new St.Button({
            style_class: 'ren-pill',
            reactive: true,
            can_focus: false,
            visible: false,
        });
        const content = new St.Widget({
            layout_manager: new Clutter.BinLayout(),
            width: 160, height: 160,
        });
        this._wave = new St.DrawingArea({
            width: 160, height: 160,
            x_align: Clutter.ActorAlign.CENTER,
            y_align: Clutter.ActorAlign.CENTER,
        });
        this._wave.set_pivot_point(0.5, 0.5);
        this._wave.connect('repaint', () => {
            const context = this._wave.get_context();
            const [width, height] = this._wave.get_surface_size();
            const motion = this._motionSettings.get_boolean('enable-animations');
            const now = GLib.get_monotonic_time();
            if (!this._feedback) {
                this._orbFrame = sampleOrb(motion ? this._orbBands : [0, 0, 0],
                    motion ? (now - this._orbStarted) / 1000000 : 0,
                    this._orbCommandMix, this._orbLoadingMix);
            }
            const frame = this._feedback ? this._feedbackFrame : this._orbFrame;
            const completion = this._feedback ? (motion ? (now - this._feedbackStarted) / 1000000 : 0.35) : null;
            if (frame)
                drawOrb(context, width, height, frame, completion);
            context.$dispose();
        });
        content.add_child(this._wave);
        this._pill.set_child(content);
        this._pill.connect('captured-event', (_actor, event) => this._capture(event));
        this._pill.connect('clicked', () => {
            if (this._state === 'recording')
                this._call('Stop');
        });
        this._pill.connect('notify::allocation', () => this._positionPill());
        Main.layoutManager.addTopChrome(this._pill);
    }

    _bindShortcut() {
        Main.wm.removeKeybinding('toggle-shortcut');
        if (!this._enabled || (this._session && this._sessionLive))
            return;
        const action = Main.wm.addKeybinding('toggle-shortcut', this._settings,
            Meta.KeyBindingFlags.IGNORE_AUTOREPEAT,
            // Live sessions temporarily remove this binding so their input
            // method receives Start and can track every finishing key release.
            Shell.ActionMode.NORMAL,
            () => this._toggle());
        if (action === Meta.KeyBindingAction.NONE)
            this._error('ren could not register this shortcut. Choose another in Preferences.');
    }

    _ownerChanged() {
        this._attached = false;
        if (!this._proxy.g_name_owner) {
            this._awaitingNativeStart = false;
            this._state = 'idle';
            this._cancelled = true;
            this._pendingText = null;
            this._drawState();
            this._finishSession();
            if (this._inserting)
                this._clearTarget();
            return;
        }
        this._call('Attach', null, () => {
            this._attached = true;
            this._sync();
        });
    }

    _call(method, parameters = null, done = null) {
        if (!this._enabled)
            return;
        if (!this._proxyReady) {
            this._error('ren is starting. Try the shortcut again in a moment.');
            return;
        }
        const enableGeneration = this._enableGeneration;
        const generation = this._generation;
        const current = proxy => this._enabled && this._proxy === proxy &&
            enableGeneration === this._enableGeneration && generation === this._generation;
        this._proxy.call(method, parameters, Gio.DBusCallFlags.NONE, 15000, this._cancellable,
            (proxy, result) => {
                try {
                    proxy.call_finish(result);
                    if (current(proxy))
                        done?.();
                } catch (error) {
                    if (!current(proxy))
                        return;
                    this._state = 'error';
                    this._awaitingNativeStart = false;
                    this._cancelled = true;
                    this._pendingText = null;
                    this._finishSession();
                    this._drawState();
                    this._error(error.message);
                }
            });
    }

    async _toggle() {
        this._indicator.menu.close();
        if (this._inserting)
            return;
        if (this._session) {
            if (this._state === 'recording')
                this._call('Stop');
            return;
        }
        if (!this._attached) {
            this._call('Attach', null, () => {
                this._attached = true;
                void this._toggle();
            });
            return;
        }
        const enableGeneration = this._enableGeneration;
        const starting = this._beginSession();
        const generation = this._generation;
        if (await starting && this._enabled && enableGeneration === this._enableGeneration &&
            generation === this._generation) {
            this._call('Toggle', null, () => {
                if (generation !== this._generation)
                    return;
                this._awaitingNativeStart = false;
                this._sync();
            });
        }
    }

    async _beginSession(nativeState = null) {
        if (this._inserting || this._session)
            return false;
        this._feedback = '';
        this._feedbackFrame = null;
        this._stopWave();
        this._clearTarget();
        this._target = global.display.focus_window;
        this._targetSignal = this._target?.connect('unmanaged', () => {
            this._targetSignal = 0;
            this._target = null;
            if (!this._clipboardOnly)
                this._cancelFromFocus();
        });
        this._targetFocusSignal = global.display.connect('notify::focus-window', () => {
            if (this._session && !this._clipboardOnly && this._target && global.display.focus_window !== this._target)
                this._cancelFromFocus();
        });
        this._heldKeys.clear();
        this._sessionFinish = parseShortcut(this._finishShortcut) ?? parseShortcut('Return');
        this._sessionStart = parseShortcut(this._settings.get_strv('toggle-shortcut')[0]);
        this._showLive = this._proxy.LivePreview !== false;
        this._sessionLive = Boolean(this._target);
        this._clipboardOnly = !this._target;
        this._finishWaiting = false;
        this._cancelled = false;
        const generation = ++this._generation;
        this._pendingText = null;
        this._latestPartial = null;
        this._autoCommand = false;
        this._commandAnimating = false;
        this._orbCommandPreview = false;
        this._session = true;
        this._preparing = true;
        this._awaitingNativeStart = nativeState === null;
        this._state = nativeState ?? 'loading';
        this._drawState();
        try {
            if (this._sessionLive) {
                this._composition = new Composition({
                    onKey: (keyval, keycode, state) => {
                        if (!this._enabled || generation !== this._generation)
                            return false;
                        return this._capture({
                            type: () => state & IBus.ModifierType.RELEASE_MASK
                                ? Clutter.EventType.KEY_RELEASE : Clutter.EventType.KEY_PRESS,
                            get_key_symbol: () => keyval,
                            get_key_code: () => keycode,
                            get_state: () => state,
                        }) === Clutter.EVENT_STOP;
                    },
                    onLost: () => {
                        if (this._enabled && generation === this._generation && !this._preparing)
                            this._cancelFromFocus();
                    },
                });
                this._bindShortcut();
                const composition = this._composition;
                try {
                    await composition.begin();
                } catch (_error) {
                    await composition.dispose();
                    if (!this._enabled || generation !== this._generation || this._cancelled)
                        return false;
                    this._composition = null;
                    this._sessionLive = false;
                    this._clipboardOnly = true;
                    this._bindShortcut();
                }
                if (!this._enabled || generation !== this._generation || this._cancelled)
                    return false;
                if (this._latestPartial && this._showLive && this._composition)
                    this._composition.update(this._latestPartial.text, this._latestPartial.stableBytes);
            }
            if (!this._sessionLive) {
                // Capture finish/cancel keys even when no input field is focused.
                this._grab = Main.pushModal(this._pill, {actionMode: Shell.ActionMode.POPUP});
                if (!(this._grab.get_seat_state() & Clutter.GrabState.KEYBOARD))
                    throw new Error('Close the system dialog and try again.');
            }
            return true;
        } catch (error) {
            if (this._enabled && generation === this._generation && !this._cancelled) {
                this._awaitingNativeStart = false;
                this._cancelled = true;
                this._pendingText = null;
                this._call('ReportError', new GLib.Variant('(s)', [error.message]));
            }
            return false;
        } finally {
            if (generation === this._generation) {
                this._preparing = false;
                if (this._cancelled || this._pendingText !== null || !ACTIVE.has(this._state))
                    this._finishSession();
            }
        }
    }

    _cancelFromFocus() {
        if (!this._session || this._cancelled)
            return;
        this._awaitingNativeStart = false;
        this._cancelled = true;
        this._pendingText = null;
        this._clearDraft();
        // An input context which lost focus cannot deliver its remaining key
        // releases. Cancel it immediately; no text is committed or sent.
        this._heldKeys.clear();
        void this._composition?.cancel();
        this._call('Cancel');
        if (!this._preparing)
            this._finishSession();
    }

    _capture(event) {
        const type = event.type();
        if (type !== Clutter.EventType.KEY_PRESS && type !== Clutter.EventType.KEY_RELEASE)
            return Clutter.EVENT_PROPAGATE;
        const key = event.get_key_symbol();
        const physicalKey = event.get_key_code();
        if (type === Clutter.EventType.KEY_RELEASE) {
            const known = this._heldKeys.delete(physicalKey);
            if (this._finishWaiting)
                this._finishSession();
            // The application may have seen a modifier press before Start.
            // Let its unmatched release through so it cannot remain stuck.
            if (this._sessionLive && !known)
                return Clutter.EVENT_PROPAGATE;
        } else {
            const repeat = this._heldKeys.has(physicalKey);
            this._heldKeys.add(physicalKey);
            if (!repeat && key === Clutter.KEY_Escape) {
                this._cancelled = true;
                this._pendingText = null;
                this._clearDraft();
                this._call('Cancel');
            } else if (!repeat && this._state === 'recording' &&
                (matchesShortcut(this._sessionFinish, event) || matchesShortcut(this._sessionStart, event))) {
                this._call('Stop');
            }
        }
        // Consume every key, including finish-chord modifiers and autorepeat.
        return Clutter.EVENT_STOP;
    }

    _sync() {
        if (!this._enabled || !this._proxy.g_name_owner)
            return;
        const shortcut = this._proxy.Shortcut;
        if (shortcut && this._settings.get_strv('toggle-shortcut')[0] !== shortcut)
            this._settings.set_strv('toggle-shortcut', [shortcut]);
        this._finishShortcut = this._proxy.FinishShortcut || 'Return';
        const next = this._proxy.State ?? 'idle';
        // Attach/property replies from before native recording can arrive while
        // the temporary input method is still being prepared.
        if ((this._preparing || this._awaitingNativeStart) && next === 'idle' && !this._cancelled)
            return;
        if (ACTIVE.has(next) || next === 'error')
            this._awaitingNativeStart = false;
        const previous = this._state;
        this._state = next;
        if (next === 'idle' && this._proxy.Message === 'Cancelled' && !this._autoCommand) {
            this._cancelled = true;
            this._pendingText = null;
            this._clearDraft();
        }
        if (ACTIVE.has(next) && !this._session && !this._inserting) {
            // Direct CLI starts also prepare the destination input method.
            const starting = this._beginSession(next);
            const generation = this._generation;
            void starting.then(ready => {
                if (!ready && this._enabled && generation === this._generation)
                    this._call('Cancel');
            });
        }
        this._drawState();
        if (!ACTIVE.has(next) && this._session)
            this._finishSession();
        if (next === 'error' && previous !== 'error')
            this._error(this._proxy.Message || 'Dictation failed.');
    }

    _drawState() {
        const recording = this._state === 'recording';
        const finishing = this._session && !this._cancelled && (this._pendingText !== null || this._inserting);
        const active = ACTIVE.has(this._state) || finishing ||
            (this._commandAnimating && !this._cancelled) || Boolean(this._feedback);
        this._recordItem.label.text = recording ? 'Finish dictation' : 'Start dictation';
        this._recordItem.setSensitive(!active || recording);
        this._panelIcon[recording ? 'add_style_class_name' : 'remove_style_class_name']('ren-panel-recording');
        this._pill.accessible_name = recording ? 'Recording. Click to finish dictation.' :
            this._state === 'loading' ? 'Starting dictation. Escape to cancel.' :
                'Transcribing. Escape to cancel.';
        if (this._feedback)
            this._pill.accessible_name = this._feedback === 'copied' ? 'Transcript copied to clipboard.' : 'Transcript inserted.';
        this._wave.visible = active;
        if (active) {
            this._startOrb();
        } else {
            this._stopWave();
            this._clearDraft();
        }
        if (active) {
            this._pill.show();
            this._positionPill();
        } else if (!this._session) {
            this._pill.hide();
        }
    }

    _startOrb() {
        if (this._orbTimeline)
            return;
        if (!this._orbStarted) {
            this._orbBands = [0, 0, 0];
            this._orbVelocity = [0, 0, 0];
            this._orbCommandMix = 0;
            this._orbLoadingMix = this._state === 'recording' ? 0 : 1;
            this._orbStarted = GLib.get_monotonic_time();
        }
        this._wave.queue_repaint();
        if (!this._motionSettings.get_boolean('enable-animations')) {
            this._orbCommandMix = Number(this._orbCommandPreview ?? false);
            this._orbLoadingMix = this._state !== 'recording' && !this._autoCommand ? 1 : 0;
            return;
        }
        let lastFrame = GLib.get_monotonic_time();
        // Follow GNOME's display frame clock.
        this._orbTimeline = new Clutter.Timeline({actor: this._wave, duration: 1000, repeat_count: -1});
        this._orbFrameSignal = this._orbTimeline.connect('new-frame', () => {
            const now = GLib.get_monotonic_time();
            const dt = Math.min(0.1, (now - lastFrame) / 1000000);
            lastFrame = now;
            const command = this._orbCommandPreview ?? false;
            this._orbCommandMix += (Number(command) - this._orbCommandMix) * (1 - Math.exp(-dt / 0.12));
            if (Math.abs(Number(command) - this._orbCommandMix) < 0.002)
                this._orbCommandMix = Number(command);
            const loading = this._state !== 'recording' && !this._autoCommand ? 1 : 0;
            this._orbLoadingMix += (loading - this._orbLoadingMix) * (1 - Math.exp(-dt / 0.28));
            const activity = voiceIntensity(this._proxy?.Level || 0);
            const bands = this._proxy?.Bands ?? [0, 0, 0];
            const peak = Math.max(0.001, ...bands);
            for (let i = 0; i < 3; i++) {
                const target = activity * bands[i] / peak;
                // Exact critically damped spring: smooth velocity at any frame rate.
                const offset = this._orbBands[i] - target;
                const velocity = this._orbVelocity[i];
                const decay = Math.exp(-14 * dt);
                const impulse = velocity + 14 * offset;
                this._orbBands[i] = target + (offset + impulse * dt) * decay;
                this._orbVelocity[i] = (velocity - 14 * impulse * dt) * decay;
            }
            this._wave.queue_repaint();
        });
        this._orbTimeline.start();
    }

    _stopOrb() {
        if (this._orbTimeline) {
            this._orbTimeline.stop();
            this._orbTimeline.disconnect(this._orbFrameSignal);
            this._orbTimeline = null;
        }
        this._orbStarted = 0;
        this._orbFrame = null;
        this._orbCommandMix = 0;
        this._orbBands = [0, 0, 0];
    }

    _stopWave() {
        this._stopOrb();
    }

    _positionPill() {
        if (!this._pill?.visible)
            return;
        const index = this._target?.get_monitor() ??
            (this._feedback ? this._orbMonitor : null) ?? Main.layoutManager.primaryIndex;
        const monitor = Main.layoutManager.monitors[index] ?? Main.layoutManager.primaryMonitor;
        if (!monitor)
            return;
        this._orbMonitor = monitor.index;
        const area = Main.layoutManager.getWorkAreaForMonitor(monitor.index);
        this._pill.set_position(
            Math.round(area.x + Math.max(0, area.width - this._pill.width - 16)),
            Math.round(area.y + Math.max(0, area.height - this._pill.height - 16)));
    }

    _receivePartial(text, stableBytes) {
        if (!this._session || this._cancelled || this._autoCommand ||
            !ACTIVE.has(this._state) || this._pendingText !== null)
            return;
        this._orbCommandPreview = isBrowserCommandPreview(text) || startsBrowserCommand(text);
        const draft = this._orbCommandPreview ? '' : text;
        this._latestPartial = {text: draft, stableBytes: draft ? stableBytes : 0};
        if (this._showLive)
            this._composition?.update(draft, draft ? stableBytes : 0);
        if (startsBrowserCommand(text)) {
            this._autoCommand = true;
            this._commandAnimating = true;
            this._pendingText = 'open browser';
            // No final decode is needed for a recognized fixed command.
            this._call('Cancel');
            const generation = this._generation;
            this._later(420, () => {
                if (generation !== this._generation)
                    return;
                this._commandAnimating = false;
                this._finishSession();
            });
        }
    }

    _clearDraft() {
        this._latestPartial = null;
        this._orbCommandPreview = false;
        this._composition?.update('', 0);
    }

    _receiveTranscript(text) {
        if (!this._session || this._cancelled || this._autoCommand)
            return;
        this._clearDraft();
        this._pendingText = insertionText(text);
        this._finishSession();
    }

    _finishSession() {
        if (!this._session || this._preparing || this._inserting ||
            (this._commandAnimating && !this._cancelled) ||
            (this._awaitingNativeStart && !this._cancelled))
            return;
        const modifiers = this._sessionLive && !this._cancelled
            ? shortcutModifiers(global.get_pointer()[2]) : 0;
        if (this._heldKeys.size > 0 || modifiers) {
            this._finishWaiting = true;
            if (!this._releaseRetry) {
                this._releaseRetry = true;
                this._later(30, () => {
                    this._releaseRetry = false;
                    if (this._finishWaiting)
                        this._finishSession();
                });
            }
            return;
        }
        this._finishWaiting = false;
        if (this._finishScheduled)
            return;
        this._finishScheduled = true;
        const generation = this._generation;
        this._later(0, async () => {
            if (generation !== this._generation)
                return;
            this._finishScheduled = false;
            if (this._heldKeys.size > 0 || (this._sessionLive && !this._cancelled &&
                shortcutModifiers(global.get_pointer()[2]))) {
                this._finishSession();
                return;
            }
            const text = this._cancelled ? null : this._pendingText;
            const openBrowser = isBrowserCommand(text);
            this._pendingText = null;
            this._inserting = true;
            // Keep the same particles on screen while input-method cleanup finishes.
            if (!text || openBrowser) {
                this._stopWave();
                this._pill.hide();
            }
            if (this._sessionLive) {
                let inserted = false;
                try {
                    if (!openBrowser && text && this._target && global.display.focus_window === this._target &&
                        !Main.overview.visible && Main.modalCount === 0) {
                        if (!await this._composition.commit(text))
                            throw new Error('The transcript could not be inserted.');
                        inserted = true;
                    } else {
                        await this._composition?.cancel();
                    }
                } catch (error) {
                    if (this._enabled && generation === this._generation && !this._cancelled) {
                        this._cancelled = true;
                        this._call('ReportError', new GLib.Variant('(s)', [error.message]));
                    }
                } finally {
                    if (generation === this._generation) {
                        this._session = false;
                        this._clearTarget();
                        this._bindShortcut();
                        if (inserted)
                            this._showFeedback('inserted');
                        else
                            this._drawState();
                        if (openBrowser && !this._cancelled)
                            this._openBrowser();
                    }
                }
                return;
            }
            this._session = false;
            this._releaseGrab();
            if (openBrowser) {
                this._clearTarget();
                this._openBrowser();
                return;
            }
            if (!text) {
                this._clearTarget();
                return;
            }
            if (this._clipboardOnly) {
                St.Clipboard.get_default().set_text(St.ClipboardType.CLIPBOARD, text);
                Main.notify('Ren', 'Transcript copied to clipboard.');
                this._clearTarget();
                this._showFeedback('copied');
                return;
            }
            if (!this._target) {
                this._insertionError('The original window closed.');
                return;
            }
            Main.activateWindow(this._target);
            this._later(120, () => this._insert(text, 0, generation));
        });
    }

    _showFeedback(kind) {
        this._feedbackFrame = this._orbFrame ?? sampleOrb([0, 0, 0], 0, 0, 1);
        this._feedbackStarted = GLib.get_monotonic_time();
        this._feedback = kind;
        this._drawState();
        const generation = this._generation;
        this._later(this._motionSettings.get_boolean('enable-animations') ? 620 : 220, () => {
            if (generation !== this._generation || this._session)
                return;
            this._feedback = '';
            this._feedbackFrame = null;
            this._drawState();
        });
    }

    _openBrowser() {
        if (!this._enabled || this._cancelled || Main.overview.visible || Main.modalCount > 0)
            return;
        try {
            const info = Gio.AppInfo.get_default_for_type('x-scheme-handler/https', false);
            if (!info)
                throw new Error('Set a default browser in Ubuntu Settings.');
            const app = Shell.AppSystem.get_default().lookup_app(info.get_id());
            if (app)
                app.activate();
            else
                info.launch([], global.create_app_launch_context(0, -1));
        } catch (error) {
            this._error(`Could not open the browser: ${error.message}`);
        }
    }

    _insert(text, attempt, generation) {
        if (generation !== this._generation)
            return;
        if (this._cancelled) {
            this._clearTarget();
            return;
        }
        if (!this._target || global.display.focus_window !== this._target || Main.modalCount > 0 || Main.overview.visible) {
            this._insertionError('The target window changed. Your transcript is available in Preferences.');
            return;
        }
        // Commit text directly; synthesizing keyvals on Mutter 46 loses any
        // Unicode character absent from the active physical keyboard layout.
        try {
            if (Main.inputMethod.currentFocus) {
                Main.inputMethod.commit(text);
                this._clearTarget();
                this._showFeedback('inserted');
                return;
            }
            const shellContext = Main.inputMethod._context?.get_object_path();
            if (this._ibusContext && this._ibusPanel && this._ibusContext !== shellContext) {
                this._ibusPanel.commit_text(IBus.Text.new_from_string(text));
                this._clearTarget();
                this._showFeedback('inserted');
                return;
            }
        } catch (error) {
            this._insertionError(`Could not insert text: ${error.message}. Your transcript is available in Preferences.`);
            return;
        }
        if (attempt < 8) {
            this._later(100, () => this._insert(text, attempt + 1, generation));
            return;
        }
        this._insertionError('This text field does not expose a Wayland or IBus input method. Your transcript is available in Preferences.');
    }

    _watchIbusPanel() {
        this._disconnectIbusPanel();
        // GNOME owns the IBus panel; reuse its commit channel without changing
        // the user's input engine. This Shell internal is pinned to GNOME 46.
        this._ibusPanel = this._ibus._panelService;
        if (!this._ibusPanel)
            return;
        this._ibusInSignal = this._ibusPanel.connect('focus-in', (_panel, path) => {
            this._ibusContext = path;
        });
        this._ibusOutSignal = this._ibusPanel.connect('focus-out', () => {
            this._ibusContext = null;
        });
        this._ibusDestroySignal = this._ibusPanel.connect('destroy', () => {
            // IBusManager can destroy its panel before emitting ready=false.
            // GObject removes the handlers; never disconnect a disposed proxy.
            this._ibusPanel = null;
            this._ibusContext = null;
            this._ibusInSignal = 0;
            this._ibusOutSignal = 0;
            this._ibusDestroySignal = 0;
        });
    }

    _disconnectIbusPanel() {
        if (this._ibusPanel) {
            this._ibusPanel.disconnect(this._ibusInSignal);
            this._ibusPanel.disconnect(this._ibusOutSignal);
            this._ibusPanel.disconnect(this._ibusDestroySignal);
        }
        this._ibusPanel = null;
        this._ibusContext = null;
        this._ibusInSignal = 0;
        this._ibusOutSignal = 0;
        this._ibusDestroySignal = 0;
    }

    _releaseGrab() {
        if (this._grab) {
            Main.popModal(this._grab);
            this._grab = null;
        }
    }

    _clearTarget() {
        this._clearDraft();
        if (this._target && this._targetSignal)
            this._target.disconnect(this._targetSignal);
        if (this._targetFocusSignal)
            global.display.disconnect(this._targetFocusSignal);
        this._targetFocusSignal = 0;
        this._target = null;
        this._targetSignal = 0;
        this._inserting = false;
        if (this._composition) {
            void this._composition.dispose();
            this._composition = null;
        }
    }

    _insertionError(message) {
        this._clearTarget();
        this._call('ReportError', new GLib.Variant('(s)', [message]));
    }

    _error(message) {
        Main.notifyError('Ren', message);
    }

    _later(delay, callback) {
        const source = GLib.timeout_add(GLib.PRIORITY_DEFAULT, delay, () => {
            this._sources.delete(source);
            if (this._enabled)
                callback();
            return GLib.SOURCE_REMOVE;
        });
        this._sources.add(source);
    }
}
