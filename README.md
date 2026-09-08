<p align="center">
  <img src="docs/wren.gif" width="160" height="112" alt="A small wren chirping, then resting.">
</p>

<h1 align="center">Ren</h1>
<p align="center">Local voice typing for GNOME.</p>

Ren turns speech into text in the focused application. Recognition runs on your
CPU, with a small particle indicator that responds to your voice.

**Ubuntu 24.04 · GNOME Shell 46 · x86-64 · English**

## Install

Build and install from source:

```bash
sudo apt install build-essential git cmake pkg-config libgtk-3-dev libpulse-dev \
  python3 gjs gir1.2-ibus-1.0 libglib2.0-bin
git clone https://github.com/w4term3loon/ren.git
cd ren
./scripts/build.sh -DGGML_NATIVE=ON
python3 scripts/install.py
```

Log out and back in to load the extension. Open **Preferences** from the wren
in the top bar, then download a model. The installer sets up both the native app
and the GNOME extension; both are required.

To update, pull the latest source and repeat the build and install steps, then
log in again. Existing Lilt settings and models are imported without removing
old data.

## Use

Focus a text field, then:

| Action | Shortcut |
| --- | --- |
| Start dictation | **Ctrl + Super + Space** |
| Finish and insert | **Enter**, the Start shortcut again, or click the indicator |
| Discard | **Escape** |

Finishing inserts text without submitting the field. **Live text** shows a draft
as you speak; turning it off hides drafts while recognition continues. Changing
focus during dictation cancels text insertion. If the field cannot accept
composition, Ren copies the finished text to the clipboard and notifies you.
**Copy to clipboard** is on by default; turn it off to keep fallback text only in
**Copy last dictation** until the next recording or Quit.

Start with **“open browser”** to launch or focus your default browser immediately.
This is a voice command: it ends dictation and keeps the phrase out of the field.

Preferences contains the model, shortcuts, Live text, and optional **Vocabulary**
hints for names and terms. **Copy last dictation** in the top-bar menu recovers
the latest result until the next recording or Quit. Microphone warnings link to
Ubuntu Sound settings.

## Models

| Model | Download |
| --- | ---: |
| Tiny English Q5_1 | 31 MiB |
| Base English Q5_1 | 57 MiB |
| Small English Q5_1 — default | 181 MiB |
| Medium English Q5_0 | 514 MiB |

[Whisper](https://github.com/openai/whisper) runs through
[whisper.cpp](https://github.com/ggml-org/whisper.cpp). Model downloads use a pinned
[ggerganov revision](https://huggingface.co/ggerganov/whisper.cpp/tree/98aa99a0a9db05ae2342309f5096248665f7cba3)
and are verified with SHA-256. **Model details** shows the size and source.

**Custom file…** accepts a local whisper.cpp GGML model and leaves it in its
original location. Compatibility is checked on load. Vocabulary hints affect
recognition only; they do not train the model.

## Privacy and limits

Audio stays in memory. Ren keeps no recording history and sends no audio for
transcription. Downloads contact Hugging Face; dictation then works offline.
The latest transcript is held in memory for recovery. Clipboard copies follow
your desktop's clipboard retention settings.

Recordings are limited to three minutes. Model weights stay ready for five
minutes after use, then leave memory. Settings live in `~/.config/ren`; downloaded
models live in `~/.local/share/ren/models`, respecting XDG overrides.

Only the platform listed above is supported. Text insertion depends on the
application's input-method support; test your usual applications before relying
on it. Recognition can make mistakes, especially with background noise. Review
text before sending it. The indicator respects GNOME's animation setting.

## Development

`src/` contains the native app; `extension/` handles GNOME input and the indicator.
`./scripts/build.sh` builds and runs three focused checks: the engine and
migration, text handling, and the install/uninstall lifecycle. Fresh builds use
portable CPU instructions; `-DGGML_NATIVE=ON` tunes for the current computer.
CMake remembers build options between runs.

Before a release, check dictation, cancellation, focus changes, clipboard fallback,
and extension disable/re-enable in a GNOME 46 session. Automated checks do not
measure transcription accuracy or establish compatibility with every application.
Report bugs through [GitHub Issues](https://github.com/w4term3loon/ren/issues),
including the Ren/GNOME versions, session type, target application, and steps to
reproduce. Add focused regression tests for bugs; keep new dependencies justified.

## Remove

```bash
python3 ~/.local/share/ren/uninstall.py
```

Add `--purge-data` to remove Ren's settings and downloaded models. Legacy data and
custom models outside Ren's data directory remain untouched.

[GPL-3.0-or-later](LICENSE) · [Third-party notices](THIRD_PARTY_NOTICES.md)
