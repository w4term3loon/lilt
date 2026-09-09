# Releasing Ren

The first public release is planned as **0.6.0**, for Ubuntu 24.04 and GNOME 46.
Nothing in the packaging script pushes code or uploads a release.

## Distribution

The GNOME store package contains only the extension. Users also need the native
Ren app and a model; the extension cannot install those through the store.
The source archive includes the native app, installer, and build instructions.
This release does not include a prebuilt native binary.

After approval, users can find Ren on [GNOME Extensions](https://extensions.gnome.org/)
or through [Extension Manager](https://github.com/mjakeman/extension-manager).
GNOME's separate [Extensions app](https://apps.gnome.org/Extensions/) manages
installed extensions. Ren's own Preferences opens from its top-bar menu.

## Prepare

1. Keep `CMakeLists.txt` and `extension/metadata.json` at the same release version.
   Add other GNOME versions only after testing them.
2. Run `./scripts/build.sh` and `git diff --check`.
   Local builds use CPU acceleration. Any future shared binary needs an explicit
   portability plan; do not distribute a build made with `GGML_NATIVE=ON`.
3. On a clean Ubuntu 24.04 session, install Ren and test live dictation, finish
   without submission, cancel, focus changes, clipboard fallback on/off, and
   voice commands on/off. Disable the extension during startup and recording;
   confirm the keyboard still works and its original input source returns.
4. Commit the release changes, then run `python3 scripts/package.py`.

The script creates three files in `dist/`:

- `ren@w4term3loon.github.io.shell-extension.zip` for GNOME review.
- `ren-0.6.0.tar.gz` for the matching source/native-companion release.
- `SHA256SUMS` for both archives.

Check the ZIP with `unzip -l dist/*.shell-extension.zip`. It must contain only
the extension's JavaScript, metadata, stylesheet, icon, schema XML, and license.
No executable, model, recording, configuration, or build directory belongs there.
The packager omits the numeric extension version because GNOME assigns it.

## Publish when ready

1. Push the reviewed commit and a `v0.6.0` tag to GitHub. Create a GitHub release
   with both archives and `SHA256SUMS`; link the installation instructions.
2. Sign in and [upload the extension ZIP](https://extensions.gnome.org/upload/).
   Use a screenshot of the actual indicator/preferences and the native-companion
   download link. The README photo can be added separately when selected.
3. Include the reviewer note below. Address feedback before announcing store
   availability; uploading does not guarantee acceptance.
4. Once approved, verify installation from the listing, install the native
   companion with `python3 scripts/install.py --app-only` after building it,
   and repeat a short dictation/command/clipboard test.
   Add the approved listing URL to the README.

Reviewer note:

> Ren requires its separately installed C++/GTK app and an English Whisper model.
> Audio capture and inference run in that app through D-Bus. The extension owns
> a temporary IBus engine for inline composition and restores the previous input
> source on completion or cancellation. Clipboard fallback is optional and on by
> default; manual copying has no keyboard shortcut. Voice commands can be disabled.

Ask reviewers to confirm that the opt-out clipboard fallback is acceptable under
their rule about default clipboard shortcuts. Recheck the current
[review guidelines](https://gjs.guide/extensions/review-guidelines/review-guidelines.html)
and [extension ZIP format](https://gjs.guide/extensions/overview/anatomy.html#extension-zip)
before uploading. A clean-session smoke test and GNOME review remain release gates.

## Small command candidates

- **Open files:** open the file manager.
- **Open terminal:** open a terminal without running anything.
- **Open settings:** open Ubuntu Settings.

These are proposals, not implemented commands. Keep explicit app names and the
global Voice commands switch; avoid interpreting arbitrary requests as commands.
