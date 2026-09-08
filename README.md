# lilt

Local English voice typing for Ubuntu 24.04 / GNOME 46 on x86-64.

Press **Ctrl + Super + Space**, speak, then **Enter** to finish or **Escape** to
cancel. Drafts appear in the focused field; finishing inserts text without
sending it. The top-bar menu opens preferences for Model, Start, Finish, and
Live text. Keep the destination field focused while dictating.

Say only **“open browser”**, then finish, to launch or focus your default browser
instead of inserting text. Longer sentences remain ordinary dictation.

Audio stays in memory. No cloud transcription, clipboard, or recording history.
Models download once, then work offline. Recordings are limited to three minutes.

## Install

```bash
sudo apt install build-essential git cmake pkg-config libgtk-3-dev libpulse-dev python3 gjs gir1.2-ibus-1.0
git clone https://github.com/w4term3loon/lilt.git
cd lilt
./scripts/build.sh -DGGML_NATIVE=ON
python3 scripts/install.py
```

Log out and back in to load the extension, then download a model in Preferences.
If a field does not support inline drafts, turn off Live text.

## Models

| English model | Parameters | Download |
| --- | ---: | ---: |
| Tiny Q5_1 | 39M | 31 MiB |
| Base Q5_1 | 74M | 57 MiB |
| Small Q5_1 (default) | 244M | 181 MiB |
| Medium Q5_0 | 769M | 514 MiB |

[OpenAI Whisper](https://github.com/openai/whisper) weights run locally through
[whisper.cpp](https://github.com/ggml-org/whisper.cpp). Quantized downloads come
from [ggerganov](https://huggingface.co/ggerganov/whisper.cpp) and are checksum verified.
Choose Base for speed or Small for accuracy.

## Project

`src/` contains the C++ app; `extension/` handles GNOME input and the indicator.
`scripts/` builds and installs; `tests/` holds two focused checks.
See the [short roadmap](docs/roadmap.md) for remaining work.

Uninstall with `python3 ~/.local/share/lilt/uninstall.py`; add `--purge-data` to
remove settings and models too.

[GPL-3.0-or-later](LICENSE) · [Third-party notices](THIRD_PARTY_NOTICES.md)
