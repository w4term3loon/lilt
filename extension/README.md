# GNOME integration

lilt 0.5.0 (extension version 6) targets Ubuntu 24.04 with GNOME Shell 46, including Wayland.
It provides the top-panel menu, global Start shortcut (default
**Ctrl+Super+Space**), and a 63 × 37 pixel bottom-center recording indicator.
Nine bars respond to microphone intensity; a lavender pulse marks processing.
The indicator has no visible text and stops animating when idle. The native C++
service owns audio capture, local recognition, and preferences.

## Live text in the destination field

With **Live text** enabled, the complete draft appears as input-method
composition at the destination cursor. There is no separate preview bubble and
no 160-character display limit. Each hypothesis replaces the previous draft.
The draft is uncommitted; existing application text and selection remain
unchanged until Finish.

The stable prefix uses the application's normal text color. The tentative suffix
requests gray foreground, and the complete draft requests an underline. IBus
clients can display these attributes; GNOME's native Wayland path may preserve
only the composition underline. The destination application controls rendering,
so two distinct colors cannot be guaranteed everywhere. A stable prefix means
wording persisted across recognition updates, not that it is certainly correct.
Even stable words remain part of the reversible draft.

The extension temporarily registers and selects a local IBus engine for the
session. This preserves application focus while supplying preedit updates and
handling dictation keys. The engine uses a separate connection to the existing
IBus daemon; cleanup closes that connection, never destroys GNOME's shared
`IBus.Bus` singleton. Cleanup restores the previous engine if lilt's temporary
engine is still selected. An input source deliberately selected by the user
takes precedence over restoration.

The destination must expose an input context with embedded preedit support.
Unavailable input services or unsupported fields produce an actionable error.
Password and PIN fields are rejected. Changing the text field, moving its cursor
in a way that resets composition, closing its window, or losing the input method
cancels the draft. Compatibility with every application is not claimed; this
integration uses GNOME internals and declares only Shell 46.

## Finish, cancel, and preferences

**Finish** defaults to **Enter**, including keypad Enter. Pressing **Start** again
also finishes recording; **Escape** discards the draft. Each session keeps the
shortcut settings it began with. Caps Lock and Num Lock do not change matching.
Application key input is consumed during dictation, and final insertion waits
for held finishing keys and modifiers to be released. The final transcript is
committed once as a single paragraph. lilt never writes to the clipboard or sends
a Return key to submit the field.

Turning **Live text** off selects final-only dictation. It disables partial
decoding and uses the established Shell input grab, followed by a direct commit
through GNOME's Wayland input method or existing IBus panel. This is an explicit
fallback for clients without live composition; it does not switch their IBus
engine. Failed final insertion leaves the latest transcript in memory and
selectable in Preferences for recovery.

Preferences contains only **Model**, **Start**, **Finish**, and **Live text**,
with download/setup/error controls appearing when needed. The four model choices
are Tiny, Base, Small, and Medium, all English. The native service always requests
English inference. Closing Preferences hides it; the top-panel menu remains.
Learning and background training are not implemented. The microphone opens only
for a dictation session.

After installing changed extension JavaScript, log out and back in to load it
into GNOME Shell. No full computer restart is required.

## Native D-Bus contract

Bus name and interface: `io.github.lilt.Dictation`.
Object path: `/io/github/lilt/Dictation`.

- Methods: `Attach`, `Detach`, `Toggle`, `Stop`, `Cancel`, `ShowPreferences`,
  `Quit`, and `ReportError(s)`.
- Properties: `State(s)`, `Level(d)`, `Message(s)`, `Shortcut(s)`,
  `FinishShortcut(s)`, and `LivePreview(b)`. `LivePreview` keeps its existing wire
  name; the preferences label is **Live text**. Missing `FinishShortcut` defaults
  to `Return`; missing `LivePreview` defaults to enabled.
- `PartialTranscript(s,u)` carries the complete hypothesis and its stable-prefix
  length in UTF-8 bytes. Live sessions convert this boundary to Unicode character
  indices for IBus attributes. Empty text clears composition. Final-only sessions
  ignore partials.
- `Transcript(s)` carries the final result and precedes `State=idle`. An explicit
  cancellation uses `Message='Cancelled'` and discards pending output.

`Attach` is repeated when the service restarts; `Detach` cancels the native
session when the extension is disabled. The native service must suppress
obsolete worker output after cancellation or a new recording: partial signals
have no session identifier. The extension also ignores drafts outside an active
session, after cancellation, and after receiving a final result. Older services
that do not emit partials can still provide final transcripts.

## Verification

Run from the repository:

```sh
gjs -m extension/test-text.js
gjs extension/tests/test-session.js
gjs extension/tests/test-lifecycle.js
bash extension/tests/headless-smoke.sh wayland
bash extension/tests/headless-smoke.sh ibus
bash extension/tests/headless-smoke.sh x11
```

The lifecycle regression delivers stale D-Bus initialization and method replies
after disable/re-enable, checks both success and failure, and verifies complete
signal cleanup. It also checks IBus panel destruction and reconnection. Shortcut
checks compare IBus parsing and case conversion against
GTK's key names, including multimedia keys, while the production extension does
not import GTK or GDK.

The headless harness requires GNOME Shell 46, GJS, Python GI, GTK3, and IBus.
It launches a private D-Bus/XDG session, a headless compositor, a fake recognizer,
and real GTK fields. `wayland` exercises native Wayland text input; `ibus`
exercises the GTK IBus module on Wayland; `x11` uses the test compositor's private
Xwayland display. It never opens a microphone or changes the current desktop's
settings.

The harness asserts draft revisions inside the field, Unicode and literal
markup, preserved selection until final commit, custom/default Finish shortcuts,
start-shortcut finishing, held-key and modifier suppression, no submission,
single final insertion, late canceled output, focus loss, target closure, and
restoration of the previous IBus engine. It also checks that no preview bubble
or Shell grab exists during a live session, validates indicator cleanup, and
saves recording/processing screenshots and state snapshots in its log directory.
These are the harness's checks; current release validation results belong in
[`docs/validation.md`](../docs/validation.md).

## Upstream sources

- [GNOME 46 input method and virtual keyboard implementation](https://github.com/GNOME/gnome-shell/blob/46.0/js/ui/keyboard.js)
- [GNOME 46 modal grabs and focus restoration](https://github.com/GNOME/gnome-shell/blob/46.0/js/ui/main.js)
- [GNOME 46 IBus panel ownership](https://github.com/GNOME/gnome-shell/blob/46.0/js/misc/ibusManager.js)
- [GNOME 46 input-method composition delivery](https://github.com/GNOME/gnome-shell/blob/46.0/js/misc/inputMethod.js)
- [IBus 1.5.29 engine preedit, focus, and key handling](https://github.com/ibus/ibus/blob/1.5.29/src/ibusengine.c)
- [IBus 1.5.29 engine proxy and asynchronous FocusId discovery](https://github.com/ibus/ibus/blob/1.5.29/bus/engineproxy.c)
- [Gio signal flushing before connection close](https://docs.gtk.org/gio/method.DBusConnection.close.html)
- [IBus 1.5.29 panel text commit routing](https://github.com/ibus/ibus/blob/1.5.29/bus/panelproxy.c)
- [Mutter 46's private accelerator parser](https://github.com/GNOME/mutter/blob/46.0/src/core/meta-accel-parse.h); lilt parses GTK modifier tokens and uses [IBus key names and case conversion](https://github.com/ibus/ibus/blob/1.5.29/src/ibuskeys.h), preserving GTK shortcut semantics without importing GDK into GNOME Shell.
