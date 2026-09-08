// SPDX-License-Identifier: GPL-3.0-or-later
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import IBus from 'gi://IBus';

import {insertionText, compositionParts} from './text.js';

const IBUS_NAME = 'org.freedesktop.IBus';
const IBUS_PATH = '/org/freedesktop/IBus';
const TIMEOUT = 3000;

function connect(address) {
    return new Promise((resolve, reject) => {
        Gio.DBusConnection.new_for_address(address,
            Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT |
            Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
            null, null, (_source, result) => {
                try {
                    resolve(Gio.DBusConnection.new_for_address_finish(result));
                } catch (error) {
                    reject(error);
                }
            });
    });
}

function call(connection, method, parameters = null, iface = IBUS_NAME,
    destination = IBUS_NAME, path = IBUS_PATH) {
    return new Promise((resolve, reject) => {
        connection.call(destination, path, iface, method, parameters,
            null, Gio.DBusCallFlags.NONE, TIMEOUT, null, (source, result) => {
                try {
                    resolve(source.call_finish(result));
                } catch (error) {
                    reject(error);
                }
            });
    });
}

async function property(connection, name) {
    const result = await call(connection, 'Get',
        new GLib.Variant('(ss)', [IBUS_NAME, name]), 'org.freedesktop.DBus.Properties');
    return result.get_child_value(0).get_variant();
}

async function engineName(connection) {
    let value = await property(connection, 'GlobalEngine');
    while (value.is_of_type(new GLib.VariantType('v')))
        value = value.get_variant();
    if (value.n_children() < 3 || value.get_child_value(0).get_string()[0] !== 'IBusEngineDesc')
        throw new Error('The system input service returned an invalid engine.');
    return value.get_child_value(2).get_string()[0];
}

async function waitForDisconnect(connection, name) {
    const deadline = GLib.get_monotonic_time() + TIMEOUT * 1000;
    while (true) {
        const reply = await call(connection, 'NameHasOwner',
            new GLib.Variant('(s)', [name]), 'org.freedesktop.DBus',
            'org.freedesktop.DBus', '/org/freedesktop/DBus');
        if (!reply.get_child_value(0).get_boolean())
            return;
        if (GLib.get_monotonic_time() >= deadline)
            throw new Error('The keyboard input service did not finish resetting.');
        await new Promise(resolve => {
            GLib.timeout_add(GLib.PRIORITY_DEFAULT, 10, () => {
                resolve();
                return GLib.SOURCE_REMOVE;
            });
        });
    }
}

function componentDescription(name) {
    // Calling serialize_object() through the installed IBus 1.5 GI bindings
    // crashes GJS while handling its floating GVariant. These D-Bus tuples are
    // the wire schema from ibusenginedesc.c and ibuscomponent.c; ordinary engine
    // text updates still use the public IBus.Engine/Text methods below.
    const engine = new GLib.Variant('(sa{sv}ssssssssussssssss)', [
        'IBusEngineDesc', {}, name, 'Dictation', 'Local dictation', 'en',
        'GPL-3.0-or-later', '', 'audio-input-microphone-symbolic', 'default', 0,
        '', '', '', '', '', '', '', '',
    ]);
    return new GLib.Variant('(sa{sv}ssssssssavav)', [
        'IBusComponent', {},
        `io.github.ren.Dictation.${GLib.uuid_string_random().replaceAll('-', '')}`,
        'Local dictation', '1', 'GPL-3.0-or-later', '', '', '', '', [], [engine],
    ]);
}

function close(connection) {
    if (!connection || connection.is_closed())
        return Promise.resolve();
    return new Promise(resolve => {
        connection.close(null, (source, result) => {
            try {
                source.close_finish(result);
            } catch (_error) {
                // A disconnected IBus daemon has already removed the component.
            }
            resolve();
        });
    });
}

function flush(connection) {
    return new Promise((resolve, reject) => {
        const cancellable = new Gio.Cancellable();
        let timeout = GLib.timeout_add(GLib.PRIORITY_DEFAULT, TIMEOUT, () => {
            timeout = 0;
            cancellable.cancel();
            return GLib.SOURCE_REMOVE;
        });
        connection.flush(cancellable, (source, result) => {
            if (timeout)
                GLib.source_remove(timeout);
            try {
                source.flush_finish(result);
                resolve();
            } catch (error) {
                reject(error);
            }
        });
    });
}

// One temporary input-method engine supplies editable composition in the actual
// destination field. IBus.Bus is a process singleton shared with GNOME Shell;
// this class deliberately owns a separate connection that it can safely close.
export class Composition {
    constructor({onKey, onLost}) {
        this._onKey = onKey;
        this._onLost = onLost;
        this._session = null;
        this._disposed = false;
    }

    begin() {
        if (this._disposed)
            return Promise.reject(new Error('Dictation is unavailable.'));
        if (this._session)
            return Promise.reject(new Error('The previous dictation is still finishing.'));
        const session = {
            name: `ren-dictation-${GLib.uuid_string_random()}`,
            originalContext: null, focusedContext: null, previousEngine: null,
            capabilities: 0, cancelled: false, closing: false, ready: false,
            committed: false, lost: false, focusSerial: 0,
            sources: new Set(),
        };
        this._session = session;
        session.startup = this._begin(session);
        return session.startup;
    }

    _check(session) {
        if (this._disposed || session.cancelled || session !== this._session)
            throw new Error('Dictation cancelled.');
    }

    async _begin(session) {
        try {
            const address = IBus.get_address();
            if (!address)
                throw new Error('The system input service is unavailable. Log out and back in.');
            session.connection = await connect(address);
            session.closedSignal = session.connection.connect('closed', () => {
                if (!session.closing)
                    this._lost(session, 'The system input service disconnected.');
            });
            this._check(session);

            session.originalContext = (await property(session.connection,
                'CurrentInputContext')).get_string()[0];
            this._check(session);
            // GNOME also ignores IBus's fallback context: it is not a text field.
            if (!session.originalContext || session.originalContext.endsWith('/InputContext_1'))
                throw new Error('Focus an editable text field and try again.');
            session.previousEngine = await engineName(session.connection);
            this._check(session);
            if (!session.previousEngine ||
                session.previousEngine.startsWith('ren-dictation-'))
                throw new Error('Focus an editable text field and try again.');
            const embedded = await property(session.connection, 'EmbedPreeditText');
            if (!embedded.get_boolean())
                throw new Error('Enable embedded preedit text in IBus Preferences.');
            this._check(session);

            session.factory = IBus.Factory.new(session.connection);
            session.factory.connect('create-engine', (_factory, name) => {
                if (name !== session.name || session.cancelled || session.closing)
                    return null;
                return this._createEngine(session);
            });
            // Set this before sending: even a timed-out reply may have registered
            // the component and require keyboard restoration after disconnect.
            session.registered = true;
            await call(session.connection, 'RegisterComponent',
                new GLib.Variant('(v)', [componentDescription(session.name)]));
            this._check(session);

            // The factory lives on this main loop: synchronous engine switching
            // would deadlock while IBus waits for our CreateEngine response.
            await call(session.connection, 'SetGlobalEngine',
                new GLib.Variant('(s)', [session.name]));
            this._check(session);
            await this._waitForFocus(session);
            this._check(session);
            const current = (await property(session.connection,
                'CurrentInputContext')).get_string()[0];
            if (current !== session.originalContext ||
                session.focusedContext !== session.originalContext)
                throw new Error('The text field changed. Start dictation again.');
            session.ready = true;
            return true;
        } catch (error) {
            session.cancelled = true;
            await this._close(session);
            throw error;
        }
    }

    _createEngine(session) {
        const engine = new IBus.Engine({
            engine_name: session.name,
            object_path: '/io/github/ren/Dictation/Engine',
            connection: session.connection,
            has_focus_id: true,
        });
        session.engine = engine;
        engine.connect('process-key-event', (_engine, keyval, keycode, state) => {
            if (session.closing || session.cancelled ||
                (session.focusedContext && session.focusedContext !== session.originalContext))
                return false;
            try {
                return Boolean(this._onKey?.(keyval, keycode, state));
            } catch (error) {
                this._lost(session, error.message);
                return true;
            }
        });
        engine.connect('focus-in-id', (_engine, context) => {
            session.focusSerial++;
            session.focusedContext = context;
            if (!session.closing && context !== session.originalContext)
                this._lost(session, 'The text field changed. Start dictation again.');
        });
        engine.connect('focus-out-id', (_engine, context) => {
            session.focusSerial++;
            if (session.focusedContext === context)
                session.focusedContext = null;
            if (!session.closing && context === session.originalContext)
                this._lost(session, 'The text field lost focus.');
        });
        // IBus discovers FocusId asynchronously. A newly registered engine can
        // receive its initial FocusIn before that query completes, so handle
        // both protocol forms and validate the unlabelled form on the bus.
        engine.connect('focus-in', () => {
            const serial = ++session.focusSerial;
            void property(session.connection, 'CurrentInputContext').then(value => {
                if (serial !== session.focusSerial || session.closing || session.cancelled)
                    return;
                session.focusedContext = value.get_string()[0];
                if (session.focusedContext !== session.originalContext)
                    this._lost(session, 'The text field changed. Start dictation again.');
            }).catch(() => this._lost(session, 'The text field lost focus.'));
        });
        engine.connect('focus-out', () => {
            session.focusSerial++;
            session.focusedContext = null;
            this._lost(session, 'The text field lost focus.');
        });
        engine.connect('set-capabilities', (_engine, capabilities) => {
            session.capabilities = capabilities;
            if (session.ready && !(capabilities & IBus.Capabilite.PREEDIT_TEXT))
                this._lost(session, 'This text field does not support live dictation.');
        });
        engine.connect('set-content-type', (_engine, purpose) => {
            if (purpose === IBus.InputPurpose.PASSWORD || purpose === IBus.InputPurpose.PIN)
                this._lost(session, 'Dictation is unavailable in password fields.');
        });
        engine.connect('reset', () => {
            if (session.ready && !session.closing && !session.committed)
                this._lost(session, 'The text cursor changed. Start dictation again.');
        });
        engine.connect('disable', () => {
            if (!session.closing)
                this._lost(session, 'The input method changed. Start dictation again.');
        });
        engine.connect('destroy', () => {
            session.engine = null;
            if (!session.closing)
                this._lost(session, 'The system input method stopped.');
        });
        return engine;
    }

    _waitForFocus(session) {
        return new Promise((resolve, reject) => {
            const deadline = GLib.get_monotonic_time() + TIMEOUT * 1000;
            const source = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 10, () => {
                let error = null;
                if (session.cancelled || this._disposed)
                    error = new Error('Dictation cancelled.');
                else if (GLib.get_monotonic_time() >= deadline)
                    error = new Error('Focus an editable text field and try again.');
                if (error) {
                    session.sources.delete(source);
                    reject(error);
                    return GLib.SOURCE_REMOVE;
                }
                if (session.engine && session.focusedContext === session.originalContext &&
                    session.capabilities & IBus.Capabilite.PREEDIT_TEXT) {
                    session.sources.delete(source);
                    resolve();
                    return GLib.SOURCE_REMOVE;
                }
                return GLib.SOURCE_CONTINUE;
            });
            session.sources.add(source);
        });
    }

    _lost(session, reason) {
        if (session.lost || session.closing || session.cancelled || this._disposed ||
            session !== this._session)
            return;
        session.lost = true;
        session.cancelled = true;
        this._clear(session);
        try {
            this._onLost?.(reason);
        } finally {
            void this.cancel();
        }
    }

    update(text, stableBytes) {
        const session = this._session;
        if (!session?.ready || session.cancelled || session.closing ||
            session.committed || session.focusedContext !== session.originalContext)
            return;
        const parts = compositionParts(text, stableBytes);
        const content = parts.stable + parts.tentative;
        const length = [...content].length;
        const preedit = IBus.Text.new_from_string(content);
        // Attribute ranges are Unicode character indices, never UTF-8 bytes or
        // JavaScript UTF-16 offsets. Explicit NONE suppresses the default
        // underline in IBus clients that honor styling. GNOME 46's native
        // Wayland path strips attributes, and clients may impose their own.
        if (length) {
            preedit.append_attribute(IBus.AttrType.UNDERLINE,
                IBus.AttrUnderline.NONE, 0, length);
            preedit.append_attribute(IBus.AttrType.FOREGROUND, 0x4f805f, 0, length);
        }
        session.engine.update_preedit_text_with_mode(preedit, length, length > 0,
            IBus.PreeditFocusMode.CLEAR);
    }

    _clear(session) {
        if (session.engine && session.focusedContext === session.originalContext)
            session.engine.update_preedit_text_with_mode(IBus.Text.new_from_string(''),
                0, false, IBus.PreeditFocusMode.CLEAR);
    }

    commit(text) {
        const session = this._session;
        if (!session)
            return Promise.resolve(false);
        if (session.finish)
            return session.finish;
        session.finish = this._commit(session, text);
        return session.finish;
    }

    async _commit(session, text) {
        let committed = false;
        try {
            await session.startup;
            this._check(session);
            const current = (await property(session.connection,
                'CurrentInputContext')).get_string()[0];
            this._check(session);
            if (session.focusedContext !== session.originalContext ||
                current !== session.originalContext || !session.engine)
                throw new Error('The text field changed. The transcript was not inserted.');
            const final = insertionText(text);
            session.committed = true;
            this._clear(session);
            if (final)
                session.engine.commit_text(IBus.Text.new_from_string(final));
            // Closing a GDBusConnection does not flush queued signals. Ensure
            // the final text reaches the transport before removing this engine.
            await flush(session.connection);
            committed = true;
        } finally {
            await this._close(session);
        }
        return committed;
    }

    cancel() {
        const session = this._session;
        if (!session)
            return Promise.resolve();
        session.cancelled = true;
        this._clear(session);
        if (!session.finish) {
            session.finish = (async () => {
                try {
                    await session.startup;
                } catch (_error) {
                    // Startup owns cleanup on failure or cancellation.
                }
                await this._close(session);
            })();
        }
        // A simultaneous commit still rejects for its caller so it can retain
        // the transcript; cancellation itself only waits for cleanup.
        return session.finish.catch(() => {});
    }

    _close(session) {
        if (!session.cleanup)
            session.cleanup = this._restoreAndClose(session);
        return session.cleanup;
    }

    async _restoreAndClose(session) {
        session.closing = true;
        session.ready = false;
        this._clear(session);
        for (const source of session.sources)
            GLib.source_remove(source);
        session.sources.clear();
        const connection = session.connection;
        let control = null;
        let restore = session.previousEngine;
        let failure = null;
        try {
            if (connection && !connection.is_closed() && session.registered) {
                control = await connect(IBus.get_address());
                const current = await engineName(control);
                // Preserve an input source explicitly selected during dictation.
                restore = current === session.name ? session.previousEngine : current;
                await flush(connection);
            }
        } catch (error) {
            failure = error;
        } finally {
            if (session.factory) {
                session.factory.destroy();
                session.factory = null;
            }
            if (connection && session.closedSignal)
                connection.disconnect(session.closedSignal);
            await close(connection);
            session.connection = null;
        }
        try {
            if (control && restore) {
                // IBus 1.5.29 checks only its dynamically registered engines when
                // a component disappears. It can therefore clear an ordinary xkb
                // engine even if we restored that engine before disconnecting.
                // Wait until the daemon removes our connection, then repair an
                // empty global engine through a connection owning no component.
                await waitForDisconnect(control, connection.get_unique_name());
                let current = null;
                try {
                    current = await engineName(control);
                } catch (_error) {
                    // No global engine is the expected 1.5.29 cleanup behavior.
                }
                if (!current || current === session.name)
                    await call(control, 'SetGlobalEngine', new GLib.Variant('(s)', [restore]));
                failure = null;
            }
        } catch (error) {
            failure = error;
        } finally {
            await close(control);
            if (session === this._session)
                this._session = null;
        }
        if (failure)
            throw new Error('The keyboard input method could not be restored. Try switching the keyboard language.');
    }

    dispose() {
        this._disposed = true;
        return this.cancel();
    }
}
