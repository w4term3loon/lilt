# GNOME integration

The extension targets GNOME Shell 46. `extension.js` owns the panel menu,
recording indicator, shortcuts, and session lifecycle. `composition.js` supplies
live text through IBus. `text.js` sanitizes transcripts and translates UTF-8
stability boundaries. The native service handles audio, recognition, settings,
and preferences; no inference runs in GNOME Shell.

## Input and cleanup

Live sessions register a temporary IBus engine on a separate connection to the
existing daemon. The engine displays the complete draft as uncommitted preedit
text inside the focused field. Stable-prefix offsets arrive in UTF-8 bytes;
IBus attributes use Unicode character indices. The application controls how
underline and tentative-text color appear.

Finish commits one sanitized paragraph after all held keys and modifiers are
released. Escape, focus loss, input-source changes, and destination closure
cancel the draft. Password and PIN fields are rejected. Closing the temporary
connection unregisters its engine; cleanup restores the prior engine unless the
user has selected another. Final signals are flushed before disconnecting.
GNOME's shared `IBus.Bus` singleton is never destroyed.

Final-only sessions use a Shell input grab, then commit through GNOME's Wayland
input method or existing IBus panel. Failed insertion leaves the transcript in
native Preferences for recovery. Neither path writes to the clipboard or sends
Return to the destination.

`disable()` cancels pending D-Bus initialization and native recording, removes
signals, keybindings, timers and actors, releases input grabs, and disposes the
temporary engine. Async callbacks are guarded by enable and session generations
so obsolete replies cannot affect a later session. The IBus panel's destruction
signal releases its reference before reconnecting to a replacement panel.

## Native D-Bus contract

Bus name and interface: `io.github.lilt.Dictation`.
Object path: `/io/github/lilt/Dictation`.

- Methods: `Attach`, `Detach`, `Toggle`, `Stop`, `Cancel`, `ShowPreferences`,
  `Quit`, and `ReportError(s)`.
- Properties: `State(s)`, `Level(d)`, `Message(s)`, `Shortcut(s)`,
  `FinishShortcut(s)`, and `LivePreview(b)`. The last property's UI label is
  **Live text**. Missing finish/live settings default to `Return`/enabled.
- `PartialTranscript(s,u)`: complete hypothesis and stable-prefix length in
  UTF-8 bytes; empty text clears composition. Final-only sessions ignore it.
- `Transcript(s)`: final result, emitted before `State=idle`. Explicit
  cancellation uses `Message='Cancelled'` and discards pending output.

`Attach` is repeated after service restarts; `Detach` cancels recording when the
extension is disabled. Partial signals have no session identifier, so the native
service must suppress worker output after cancellation or a newer recording.
The extension also rejects drafts outside an active session, after cancellation,
and after final output. Each session keeps the shortcut settings it began with.
