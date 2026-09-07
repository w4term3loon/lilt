# lilt

Local English voice typing for **Ubuntu 24.04 · GNOME 46 · x86-64**.
[Source](https://github.com/w4term3loon/lilt) · [Roadmap](docs/roadmap.md) · [Development](CONTRIBUTING.md)

## Use

1. Focus a text field and press **Ctrl + Super + Space**.
2. Speak; the bars follow your voice and draft text appears in the field.
3. Press **Enter** to finish or **Escape** to discard. Three dots mark final processing.

Finish inserts text without submitting it. Start again also finishes. Set Model,
Start, Finish, and Live text from the top-bar microphone or the lilt launcher.

Audio stays in memory: no cloud transcription, telemetry, clipboard access, or
recording history. Models download on request; dictation then works offline.
The microphone closes before finalization; only model weights stay cached for
60 seconds after a session, with fresh decoder state for each recording.

## Install

```bash
sudo apt install build-essential git cmake pkg-config libgtk-3-dev libpulse-dev python3 binutils
git clone https://github.com/w4term3loon/lilt.git
cd lilt
./scripts/build.sh
python3 scripts/install.py
```

This installs the native app and extension for your user. **Log out and back in**
to load the extension, then download a model in Preferences. An extracted native
release archive includes the same installer and needs no compilation.

Updates also need a new login. Legacy PTT settings/models migrate without
replacing lilt data or deleting old files; the old extension is disabled.

## Models

| English model | Parameters | Download |
| --- | ---: | ---: |
| Tiny Q5_1 | 39M | 30.7 MiB |
| Base Q5_1 | 74M | 57.0 MiB |
| Small Q5_1 — default | 244M | 181.3 MiB |
| Medium Q5_0 | 769M | 514.2 MiB |

[OpenAI Whisper](https://github.com/openai/whisper#available-models-and-languages)
weights run locally on the CPU through whisper.cpp. Downloads are quantized by
[ggerganov/whisper.cpp](https://huggingface.co/ggerganov/whisper.cpp/tree/98aa99a0a9db05ae2342309f5096248665f7cba3)
and verified against pinned sizes and SHA-256 hashes. Download size is not RAM use.
Settings: `~/.config/lilt/config.ini`; models: `~/.local/share/lilt/models` (XDG overrides apply).

**Optional vocabulary (experimental):** create `~/.config/lilt/vocabulary.txt`
with one name or term per line (UTF-8, at most 1,024 bytes; XDG overrides apply).
Whisper uses at most 128 prompt tokens as hints, without training the model.
An absent or empty file disables hints; deleting it disables them from the next
recording.

## Limits

English only; recordings last at most three minutes. Drafts can change until
Finish. Keep the field focused; switching fields cancels. Password/PIN fields
are excluded. Failed insertion leaves selectable recovery text in Preferences
until the next recording or exit. Review text before sending.

Drafts request faint green text without an underline. GNOME's native Wayland
path and Chromium can override styling. Disable Live text for incompatible
fields. For missing audio, check Ubuntu Sound's default microphone.

## Code map

| Location | Purpose |
| --- | --- |
| `src/` | C++ audio capture, Whisper inference, GTK preferences, D-Bus service |
| `extension/` | GNOME shortcuts, indicator, IBus composition and final insertion |
| `scripts/`, `tests/` | Build, install, package, and isolated checks |

## Remove

Run `python3 ~/.local/share/lilt/uninstall.py` to keep settings and models, or
add `--purge-data` to delete them. Legacy PTT data is preserved.

Copyright © 2026 lilt contributors. GPL-3.0-or-later, without warranty.
See [LICENSE](LICENSE), [third-party notices](THIRD_PARTY_NOTICES.md), and [changes](CHANGELOG.md).
