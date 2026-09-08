# Ren

Local English voice typing for Ubuntu 24.04 / GNOME 46 on x86-64.

Press **Ctrl + Super + Space**, speak, then **Enter** to finish or **Escape** to
cancel. Drafts appear in the focused field; finishing inserts text without
sending it. The top-bar menu opens preferences for Model, Start, Finish, and
Live text. Without a supported text field, finished dictation goes to the clipboard.

Start with **“open browser”** to launch or focus your default browser automatically.
The command stays out of the text field; the particles expand in purple, then disappear.
Live recognition runs with Live text off too; that switch only hides inline drafts.

The cloud stays in the bottom-right corner, clear of panels and docks. Click it to finish.

Audio stays in memory. No cloud transcription or recording history.
Models download once, then work offline. Recordings are limited to three minutes.

## Install

```bash
sudo apt install build-essential git cmake pkg-config libgtk-3-dev libpulse-dev python3 gjs gir1.2-ibus-1.0
git clone https://github.com/w4term3loon/ren.git
cd ren
./scripts/build.sh -DGGML_NATIVE=ON
python3 scripts/install.py
```

Log out and back in to load the extension, then download a model in Preferences.
Existing Lilt settings and downloaded models are imported automatically.

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
Choose Base for speed or Small for accuracy. **Model details** shows the file size and source.
Choose **Custom file…** to use a local whisper.cpp GGML model; compatibility is checked
when loaded. Custom files stay in their original location.

The cloud listens, the ring processes, and purple expansion recognizes a command.
On completion, the ring gathers into one dot and fades. Clipboard output also shows a notification.
Ren respects Ubuntu’s animation setting.

## Project

`src/` contains the C++ app; `extension/` handles GNOME input and the indicator.
`scripts/` builds and installs; `tests/` holds two focused checks.
See the [short roadmap](docs/roadmap.md) for remaining work.

Uninstall with `python3 ~/.local/share/ren/uninstall.py`; add `--purge-data` to
remove settings and models too.

[GPL-3.0-or-later](LICENSE) · [Third-party notices](THIRD_PARTY_NOTICES.md)
