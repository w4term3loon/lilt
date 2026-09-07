# Development

User setup is in [README](README.md); planned work is in [the roadmap](docs/roadmap.md).
Keep changes focused and private audio, text, settings, weights, and diagnostics
out of Git. Report the problem, resulting behavior, and relevant verification.
For bug reports include `lilt --version`, GNOME/session type, model, destination
application, and reproduction steps; remove private content from diagnostics.

## Build and test

`./scripts/build.sh` builds and runs the available core tests. For desktop checks:

```bash
sudo apt install gjs gir1.2-gtk-3.0 gir1.2-ibus-1.0 ibus-gtk3 xvfb dbus-x11 python3-gi
./scripts/build.sh
for mode in wayland ibus x11; do
    bash extension/tests/headless-smoke.sh "$mode"
done
```

Core tests cover decoding/cancellation, Unicode, legacy migration, service startup,
shortcut/session cleanup, and installation safety. The desktop harness uses
private D-Bus/XDG/display sessions and synthetic recognition, without a microphone
or changes to the active desktop. It checks revisions, selection replacement,
held keys, final insertion without submission, cancellation, and engine restoration.
Passing fixtures establish neither general application compatibility nor accuracy.

Add focused regressions for actual failures; avoid duplicate scenarios. Inspect
bars at low/high levels, bouncing finalization dots, and light/dark preferences
when styling changes. Check inline appearance in a real destination field.

## Runtime boundaries

`src/app.cpp` owns settings, preferences, and the D-Bus service; `engine.cpp` owns
PulseAudio capture and whisper.cpp decoding. Ubuntu normally serves PulseAudio
through PipeWire. No inference runs in GNOME Shell.

`extension.js` owns shortcuts, indicator, and session lifecycle; `composition.js`
registers a temporary IBus engine for uncommitted inline drafts. `text.js`
sanitizes text and maps UTF-8 byte boundaries to Unicode character indices.
GTK IBus honors the requested green foreground and disabled underline; GNOME 46's
native Wayland path and other applications may impose their own composition style.

Finish commits once after held keys/modifiers are released. Escape, focus loss,
input-source changes, and target closure cancel. Cleanup closes the private IBus
connection and restores the previous engine unless the user changed it; never
destroy GNOME's shared IBus singleton. Final-only mode uses a Shell input grab
and GNOME/IBus insertion. Neither path uses the clipboard or submits the field.

Disable removes timers, actors, signals, grabs, and the temporary engine. Native
and extension generation guards reject obsolete asynchronous work after restart,
cancellation, or a new session. Preserve these guards when simplifying code.

## D-Bus contract

Name/interface: `io.github.lilt.Dictation`; path: `/io/github/lilt/Dictation`.

- Methods: `Attach`, `Detach`, `Toggle`, `Stop`, `Cancel`, `ShowPreferences`, `Quit`, `ReportError(s)`.
- Properties: `State(s)`, `Level(d)`, `Message(s)`, `Shortcut(s)`, `FinishShortcut(s)`, `LivePreview(b)`.
- `PartialTranscript(s,u)` carries the full draft and stable-prefix UTF-8 byte count; empty text clears it.
- `Transcript(s)` carries final text before idle. Cancellation reports `Message='Cancelled'`.

Only the Shell owner can attach. Reattach after native service restarts; detach
cancels recording. Partial signals lack session IDs, so native generation guards
must suppress stale output. Each session retains its initial shortcut settings.

## Release and installation

```bash
python3 scripts/package.py --output dist
python3 tests/test_packaging.py --archives dist
```

CI checks core, desktop, and archives. Packages contain the native app, extension,
corresponding source with pinned whisper.cpp, licenses, and checksums; no models
or system libraries. Releases require baseline x86-64 builds. Use
`./scripts/build.sh -DGGML_NATIVE=ON` only for local CPU optimization.

Installation defaults to `~/.local` and respects `XDG_DATA_HOME`. For staging use
`python3 scripts/install.py --prefix /usr --data-dir /usr/share --destdir /tmp/lilt-stage`.
`--destdir` redirects writes without contacting the desktop; `--no-enable` skips
extension-state changes. Custom data paths must be discoverable through XDG.
The installed uninstaller remembers the layout; `--user-data-dir`/`--config-dir`
select runtime data/config bases when purging. Ordinary removal preserves both.

Contributions use GPL-3.0-or-later. Preserve [third-party notices](THIRD_PARTY_NOTICES.md).
Add GNOME versions to metadata only after testing them.
