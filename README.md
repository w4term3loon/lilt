# Ren

Local voice typing for Ubuntu. Speak into the focused application, see text as
you talk, and finish without sending the message.

Supports Ubuntu 24.04 with GNOME Shell 46 on x86-64. Transcription is English-only
and runs on your CPU.

## Install

Ren needs both the native app and the GNOME extension. Build and install them:

```bash
sudo apt install build-essential git cmake pkg-config libgtk-3-dev libpulse-dev \
  python3 gjs gir1.2-ibus-1.0 libglib2.0-bin
git clone https://github.com/w4term3loon/ren.git
cd ren
./scripts/build.sh
python3 scripts/install.py
```

Log out and back in, open **Preferences** from Ren's top-bar menu, and download
a model. To update, pull the source and repeat the build and install steps.
Existing settings and models are preserved.

If you installed the extension from GNOME Extensions, use
`python3 scripts/install.py --app-only` instead. This installs or updates the
native app while leaving the store-managed extension untouched.

## Use

| Action | Shortcut |
| --- | --- |
| Start | **Ctrl + Super + Space** |
| Finish | **Enter**, the Start shortcut again, or click the indicator |
| Discard | **Escape** |

Shortcuts are configurable. **Live text** controls the draft shown while speaking.
Changing focus cancels insertion. When there is no writable field, **Copy to
clipboard** copies the result instead; this is optional and enabled by default.
**Copy last dictation** remains available until the next recording or Quit.

Say **“open terminal”**, **“open browser”**, or **“open” followed by an installed
app's full name** to launch or focus it through GNOME. Browser uses your default;
terminal opens GNOME Terminal, or Console when Terminal is unavailable.

A complete, unambiguous name executes without Enter and stays out of the text
field. If it could become a longer app name, press Enter to confirm the shorter
one. Unknown or ambiguous names remain dictation. Turn off **Voice commands** to
treat every phrase as dictation.

## Models

| English model | Download |
| --- | ---: |
| Tiny Q5_1 | 31 MiB |
| Base Q5_1 | 57 MiB |
| Small Q5_1 (default) | 181 MiB |
| Medium Q5_0 | 514 MiB |

[whisper.cpp](https://github.com/ggml-org/whisper.cpp) runs
[OpenAI Whisper](https://github.com/openai/whisper) weights locally. Downloads use
a pinned [ggerganov conversion](https://huggingface.co/ggerganov/whisper.cpp/tree/98aa99a0a9db05ae2342309f5096248665f7cba3)
and SHA-256 verification. **Model details** shows each file's size and source.

**Custom file…** accepts compatible whisper.cpp GGML models. **Vocabulary** adds
optional hints for names and terms; it does not train the model.

## Privacy and limits

Audio stays in memory and is never sent for transcription. Models download once
from Hugging Face. The latest transcript stays in memory for recovery; copied
text follows your desktop's clipboard retention settings.

Recordings last up to three minutes. Model weights stay warm for five minutes,
then leave memory. Input-method support varies between applications.

## Development and releases

`src/` contains the native app; `extension/` contains GNOME integration and visuals.
The build script runs the existing engine, text, and installation checks.
Builds use your CPU's available instructions. Use `-DGGML_NATIVE=OFF` only when
building a portable binary for another machine; this is slower for transcription.

See [RELEASING.md](RELEASING.md) for packaging, GNOME submission, and rollout.
Report problems through [GitHub Issues](https://github.com/w4term3loon/ren/issues).

## Remove

```bash
python3 ~/.local/share/ren/uninstall.py
```

Use `--app-only` to keep a store-managed extension installed.
Add `--purge-data` to remove Ren's settings and downloaded models. Custom model
files stay in their original locations.

[GPL-3.0-or-later](LICENSE). [Third-party notices](THIRD_PARTY_NOTICES.md).
