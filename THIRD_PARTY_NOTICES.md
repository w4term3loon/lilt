# Third-party notices

lilt's original source is licensed under **GPL-3.0-or-later**. Third-party
components retain their own licenses and copyright notices.

## whisper.cpp and ggml

The native executable statically links whisper.cpp and its ggml CPU backend.
The build fetches whisper.cpp **v1.9.3**, pinned to Git tag object
`7246b7311e089fe092c4abe7cfad5d0921f8be00`.

- Source: <https://github.com/ggml-org/whisper.cpp/tree/v1.9.3>
- License: MIT, Copyright (c) 2023–2026 The ggml authors.
- The complete upstream notice is included in
  [LICENSES/whisper.cpp-MIT.txt](LICENSES/whisper.cpp-MIT.txt).

## Whisper model weights

Model files are downloaded separately on explicit user request. They are not
included in the source repository. The downloader pins the
publisher revision, file sizes, and SHA-256 digests.

- Original model source and MIT license:
  <https://github.com/openai/whisper#license>
- Converted weights:
  <https://huggingface.co/ggerganov/whisper.cpp/tree/98aa99a0a9db05ae2342309f5096248665f7cba3>

## System libraries

GTK, GLib/GIO, PulseAudio client libraries, the C/C++ runtime, OpenMP runtime,
GNOME Shell, GJS, and IBus are supplied by the operating system. The Ubuntu
package manager supplies their corresponding license notices and sources.
