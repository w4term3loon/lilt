# lilt

Minimal local voice typing for **Ubuntu 24.04 / GNOME 46**, including Wayland.
A native C++ app handles audio and transcription. A small GNOME extension puts a
microphone in the top bar, captures the shortcut and finish key, and inserts text.

![Live dictation in a text field](docs/live-preedit.png)

Synthetic text in the isolated GNOME integration test. Styling depends on the
destination application.

**Supported:** Ubuntu 24.04, GNOME Shell 46, x86-64, English dictation.
Wayland is the primary desktop target. Native Wayland, GTK/IBus, and XWayland
input paths have isolated integration coverage. Compatibility with arbitrary
applications or other GNOME releases is not assumed.

1. Focus a text field.
2. Press **Ctrl + Super + Space**, then speak.
3. Recognized words appear and update inside that field.
4. Press **Enter** to finish (default) and finalize the transcript.

Set **Start** and **Finish** independently in Preferences. Finish can be a single
key or a combination. Your existing Start shortcut is preserved when upgrading.
**The finish key never submits your message.** Escape discards the recording.
The Start shortcut can also finish recording. Close the preferences
window to hide it; reopen it from the top-bar microphone.

## Model

English only. Default: **Whisper Small English Q5_1**, 181 MiB, running on CPU through
[whisper.cpp](https://github.com/ggml-org/whisper.cpp). Base (57 MiB), Tiny (31 MiB),
and Medium (514 MiB) are available in the same small settings window. All four
choices use English models. Existing English model selections are preserved;
older multilingual selections migrate to the English model of the same size.

Audio stays in memory. The microphone is opened only for recording. Once it is
ready, the model loads in the background while audio capture continues. A
separate worker recognizes partial text during recording. The microphone closes
before final decoding, and model memory is released after each dictation. There
is no cloud transcription, audio history, telemetry, clipboard access, background
training, or chat model.
The only network operations are the initial build dependency fetch and explicit
model downloads. Python is used for setup/downloads only; the application is C++.

The [model comparison](docs/models.md) explains the choice, other engines, licenses,
and why file size, RAM, accuracy, and speed are different tradeoffs.
The [live transcription investigation](docs/live-transcription.md) records the
model-training options and measured limits of streaming and text composition.

## Build and install

```bash
sudo apt install build-essential git cmake pkg-config libgtk-3-dev libpulse-dev python3 binutils
./scripts/build.sh
python3 scripts/install.py
```

Open **lilt** from Applications to download a model, or run
`python3 scripts/download-model.py` from the checkout. The Small English model
needs about 181 MiB of disk space; other choices are listed above.

On Ubuntu 24.04 x86-64, the development libraries can also be unpacked inside the
project without sudo. A working compiler, Make, Git, Python, and Ubuntu package
tools must already be installed:

```bash
python3 scripts/bootstrap-deps.py
./scripts/build.sh
python3 scripts/install.py
```

The installer copies the executable to `~/.local/bin/lilt`, registers a launcher and
session D-Bus service, and installs/enables `lilt@local` for your user. **Log out and
back in once** so GNOME discovers the new extension. lilt then appears in the top
bar. The background app starts on the first menu action or shortcut; it does not
load a model or open the microphone at login. Search **lilt** in Applications for preferences.

The build pins whisper.cpp v1.9.3 to immutable Git tag object
`7246b7311e089fe092c4abe7cfad5d0921f8be00`. Default builds target baseline x86-64
CPUs. For a build optimized for your own computer, use
`./scripts/build.sh -DGGML_NATIVE=ON`; those executables are unsuitable for general
distribution. GPU setup is unnecessary.

## Release packages

To prepare local artifacts from a tested portable build:

```bash
./scripts/build.sh
python3 scripts/package.py --output dist
```

The output includes a native installation archive with its matching extension,
a separate extension ZIP, a source archive, and SHA-256 checksums. Native
archives target **Ubuntu 24.04 x86-64** and depend on the system GTK, PulseAudio,
OpenMP, C/C++, and GNOME/IBus libraries. They do not include models.

To install a native archive, extract it, enter its directory, and run
`python3 scripts/install.py`. Download a model from Preferences after logging
back in. The standalone extension ZIP requires the native application to be
installed separately.

The source archive includes the pinned whisper.cpp source needed to build its
matching executable. Review [CHANGELOG.md](CHANGELOG.md) and the checksums when
preparing a release. The CI workflow builds and checks artifacts; publishing a
GitHub release is a separate manual action.

For staging or packaging without changing the active desktop:

```bash
python3 scripts/install.py --prefix /usr --data-dir /usr/share --destdir /tmp/lilt-stage
```

`--no-enable` installs files without changing desktop settings. `--prefix` and
`--data-dir` accept absolute paths; `--destdir` redirects writes into a staging
tree while preserving the final paths in launchers and service files.

For a live installation with a custom executable prefix, keep `--data-dir` in
GNOME's normal user data location or configure `XDG_DATA_DIRS` for discovery.
Installing into an arbitrary directory does not register it with GNOME or the
session bus. Models and settings use the application's runtime XDG directories,
independently of the executable prefix.

The repository URL and remote are intentionally unset until a hosting location
is chosen. The extension currently uses the provisional UUID `lilt@local`.

## Upgrading from the PTT prototype

The new command is `lilt`. On first application startup, legacy `ptt` settings and
model files are imported into the corresponding `lilt` directories without
overwriting existing lilt files. Existing model selections and shortcuts are
preserved. Old files remain available for recovery. The installer disables the
old `ptt@local` extension to avoid conflicting shortcuts.

Log out and back in after upgrading: GNOME Shell caches extension JavaScript for
the login session. If the application was already running, quit it before using
the new version.

## Repository layout

| Directory | Purpose |
| --- | --- |
| `src/` | Native C++ audio, recognition, preferences, and D-Bus service |
| `extension/` | GNOME Shell integration and its integration tests |
| `data/` | Desktop artwork and application assets |
| `scripts/` | Build, installation, packaging, and model download tools |
| `tests/` | Native, activation, migration, and packaging regressions |
| `docs/` | Research, compatibility evidence, and screenshots |
| `.deps/` | Ignored Ubuntu development packages extracted without sudo |
| `build/` | Ignored generated build tree, including fetched whisper.cpp |

Generated dependencies, model weights, local logs, and builds are excluded from
Git. Release packages include only the files needed to install and run lilt.

## Desktop behavior and limits

- A tiny 63×37 indicator near the bottom center contains nine rounded bars.
  Ivory bars react to voice intensity; lavender bars pulse while processing.
  There is no visible text or recording dot, and animation stops when idle.
- Live words use the destination field's input-method composition, with no
  separate text bubble. Words can be revised until Finish; Escape discards the
  draft. IBus applications can display tentative words in gray, while GNOME's
  native Wayland path generally uses the application's composition underline.
  Agreement between updates does not guarantee correctness. Turn off **Live
  text** in Preferences for final-only recognition and insertion.
- Live updates use repeated Whisper inference, with at most one pending audio
  snapshot. They appear after a few seconds and can slow down with longer
  recordings or larger models. Medium is available for evaluation; Base is more
  responsive on this machine. This is not a native streaming recognizer.
- A temporary IBus engine handles live composition and consumes dictation keys.
  Finish waits for held keys and modifiers before committing, without a Return
  or clipboard operation. The previous input method is restored afterward;
  switching the input source manually takes precedence. Final-only mode keeps
  the established GNOME keyboard grab. Shortcuts stay fixed during a recording.
- Text is committed directly through GNOME's Wayland/IBus input methods. No
  synthetic Return or clipboard operation is used. Line breaks and control
  characters are normalized to spaces.
- Live text requires a working IBus composition context with embedded preedit
  enabled. Some custom fields do not support it. Unsupported fields report an
  error; final-only mode remains available. A failed final insertion leaves the
  transcript in Preferences for manual recovery.
- The text field/caret should remain in place while dictating. Changing the
  destination or closing its window cancels the session. Existing selected text
  is replaced only when the final transcript is committed.
- The default microphone comes from Ubuntu Sound settings. Dictations are capped
  at three minutes. Nothing is saved to disk.
- This version targets GNOME 46. Other Ubuntu desktops and GNOME versions are
  not claimed as supported. GNOME Shell input-method internals require testing
  when upgrading the desktop.

An error-recovery transcript is retained in RAM until the next recording or
application exit. Recognition can make mistakes or hallucinate on noise; review
text before sending it. Very quiet/brief input is ignored.

## Command line

```bash
~/.local/bin/lilt                 # preferences
~/.local/bin/lilt --daemon        # background service
~/.local/bin/lilt --toggle        # requires active GNOME integration
~/.local/bin/lilt --stop
~/.local/bin/lilt --cancel
~/.local/bin/lilt --quit

# Offline test; does not open the microphone or insert anything:
./build/lilt --transcribe ~/.local/share/lilt/models/ggml-small.en-q5_1.bin audio.wav en
```

Offline WAV files must be mono, 16 kHz, PCM16 or float32. Settings live at
`~/.config/lilt/config.ini`; models at `~/.local/share/lilt/models`. XDG config/data
overrides are respected. The model downloader verifies pinned sizes and SHA-256
checksums and publishes files atomically.

## Verification

`./scripts/build.sh` also runs the native tests: control-character and Unicode
normalization, malformed/silent/short WAVs, and cancellation/restart against a
stalled fake audio server. These tests never open a real microphone.

When Python, GJS, D-Bus and Xvfb are available, the build also runs a service
activation regression: the real extension proxy starts the native app from a
stopped state, reopens it after Quit, and checks that its interface is available
on the very first call. Run this test directly with
`python3 tests/native_activation.py`. It uses private X/D-Bus sessions and an
unavailable audio server.

Additional desktop smoke test (requires `python3-gi`, GTK introspection, `xvfb`):

```bash
dbus-run-session --config-file tests/session-bus.conf -- \
  xvfb-run -a -s '-screen 0 800x640x24' python3 tests/app_smoke.py
gjs -m extension/test-text.js
gjs extension/tests/test-session.js
bash extension/tests/headless-smoke.sh
```

The D-Bus smoke test disables desktop-service activation, checks single-instance
behavior, trusted Shell attachment, cancellation, persistent errors, and renders
the preferences UI with an intentionally unavailable audio server.
See [validation notes](docs/validation.md) for measurements and desktop evidence.

## Troubleshooting

- **No microphone icon:** check `gnome-shell --version`, log out and back in,
  and enable lilt in the Extensions application. Only GNOME 46 is supported.
- **Native service unavailable:** confirm `~/.local/bin/lilt` exists and open it
  directly. Install both the native application and extension; the extension ZIP
  alone does not perform recognition.
- **No model or a failed download:** use **Download model** in Preferences. A
  network connection is needed for downloads, but dictation works offline once
  the selected model is installed.
- **No audio:** select the desired default microphone in Ubuntu Sound settings.
- **Live text unavailable in one application:** turn off **Live text** and retry.
  Password/PIN fields are excluded. Keep the target field focused while dictating.
- **Shortcut conflict:** choose a different Start shortcut in Preferences.
- **A failed final insertion:** Preferences retains selectable recovery text in
  memory until the next recording or application exit.

When reporting a problem, include the release version, GNOME version, session
type, target application, and reproduction steps. See [CONTRIBUTING.md](CONTRIBUTING.md).

## Remove

```bash
python3 ~/.local/share/lilt/uninstall.py
```

The uninstaller disables the extension and removes lilt's executable, launcher,
service, and runtime files. It preserves settings and models. Add `--purge-data`
to also remove lilt's settings and models. Legacy PTT files are preserved.

For a custom installation prefix, the uninstaller is in
`PREFIX/share/lilt/uninstall.py`; it remembers the installation's data directory.
When purging models from a custom runtime data location, use
`--user-data-dir /absolute/data/base`; `--config-dir` selects the configuration
base. Both default to the current user's XDG directories.

## License

Copyright © 2026 lilt contributors. lilt is free software under
**GPL-3.0-or-later**: you may redistribute and modify it under version 3 of the
GNU General Public License, or any later version. It is distributed without any
warranty. See [LICENSE](LICENSE) and [third-party notices](THIRD_PARTY_NOTICES.md).
