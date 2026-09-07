# Local transcription model choice

Research checked 7 September 2026. The default is **Whisper small.en Q5_1**, an
English speech recognition model with a 181.3 MiB weight file, executed by
`whisper.cpp` on the CPU. This is a practical quality/footprint choice for short
desktop dictation, not a claim that it is the most accurate or smallest model
available. Actual speed and accuracy depend on the CPU, microphone, language,
accent, and recording.

Speech recognition still requires a trained statistical model. The app only
runs inference; it does not train anything, require a language-model chat server,
or upload speech. C++ describes the implementation of the inference engine,
not an alternative to machine learning.

## Supported choices

| Model | Download | Use when |
| --- | ---: | --- |
| `medium.en-q5_0` | 514.2 MiB | You want to evaluate a larger 769-million-parameter model and accept more processing time and memory. |
| `small.en-q5_1` (default) | 181.3 MiB | Recognition quality takes priority within a compact CPU model. |
| `base.en-q5_1` | 57.0 MiB | You want a faster, smaller compromise. |
| `tiny.en-q5_1` | 30.7 MiB | Minimum footprint matters more than recognition quality. |

Medium uses the publisher's Q5_0 quantization. Small remains the default. Downloading
Medium does not select it or change existing preferences. Its greater parameter
count does not establish an accuracy improvement for a particular recording;
compare it against Small on the same audio before switching.

Sizes are the actual files in the
[publisher's pinned model repository](https://huggingface.co/ggerganov/whisper.cpp/tree/98aa99a0a9db05ae2342309f5096248665f7cba3).
The English variants can improve English accuracy, especially at tiny and
base sizes, according to [OpenAI's model guidance](https://github.com/openai/whisper#available-models-and-languages).
The desktop app supports English only and always passes `en` to inference.
Existing English selections are retained; legacy multilingual selections migrate
to the same model size in English. Migration does not download weights: if that
English file is absent, Preferences shows **Download model**. Old recordings and
model files are not deleted.

**Download size is not runtime RAM.** Quantization compresses the weights;
the decoder, audio buffers, and working tensors still need memory. The
`whisper.cpp` documentation gives unquantized reference figures of approximately
273 MB for tiny, 388 MB for base, and 852 MB for small. These are neither measured
lilt usage nor promises about Q5_1 memory. The engine supports CPU inference,
quantized weights, and a C API on Linux; its code is MIT licensed.
[Engine documentation](https://github.com/ggml-org/whisper.cpp#memory-usage)

## Alternatives considered

| Engine/model | Relevant tradeoff |
| --- | --- |
| Vosk small English | A 40 MB download; the project estimates about 300 MB runtime RAM for small models. Streaming and configurable vocabulary are useful. The English model is Apache-2.0 licensed. Punctuation/case restoration can require another model. |
| sherpa-onnx / Zipformer | Native C/C++ APIs and genuine streaming recognition, including compact language-specific models. It adds ONNX Runtime and separate model components. Engine: Apache-2.0; model licensing is checked per model. |
| Moonshine Voice | A strong candidate for English with native C/C++ and streaming model families. Code and English models are MIT licensed; other language models have different community terms. It deserves a direct latency/accuracy comparison if English-only dictation is the priority. |
| NVIDIA Parakeet TDT 0.6B v3 | 600 million parameters, automatic punctuation, and 25 European languages including Hungarian. CC-BY-4.0. A candidate for higher-throughput multilingual recognition, but beyond this app's initial small single-file backend. |

The Vosk figures and punctuation caveat come from its
[official model catalog](https://alphacephei.com/vosk/models).
The other comparisons use [sherpa-onnx's native engine documentation](https://github.com/k2-fsa/sherpa-onnx),
[Moonshine's native engine documentation](https://github.com/moonshine-ai/moonshine)
and [license](https://github.com/moonshine-ai/moonshine/blob/main/LICENSE), and
[NVIDIA's Parakeet model card](https://huggingface.co/nvidia/parakeet-tdt-0.6b-v3).
These projects report results on different hardware and datasets, so their
headline speeds and error rates do not establish a fair ranking for this app.

`whisper.cpp` was selected because it fits directly into a small native app,
offers English models with a simple size ladder, and uses one weight file.
This is an engineering choice. A meaningful personal comparison uses the same
20–30 recordings, including names, technical terms, pauses, and background noise,
and measures transcription errors, time after stopping, and peak RAM on the
actual computer. No claim of a measured accuracy win is made here.

## Download and verification

From the project directory:

```sh
python3 scripts/download-model.py
python3 scripts/download-model.py medium.en-q5_0
python3 scripts/download-model.py base.en-q5_1
python3 scripts/download-model.py tiny.en-q5_1
python3 scripts/download-model.py --list
```

The default directory is `$XDG_DATA_HOME/lilt/models` or
`~/.local/share/lilt/models`. Preferences offers four English models and checks
their files in this directory. The CLI offers the same four English models.
Use `--directory /path/to/models` for separate CLI-only model storage. An existing valid file is
reused. An invalid existing file is preserved unless `--force` is supplied.

The downloader uses Python's standard library only for initial setup. Downloads
use an immutable Hugging Face revision, a pinned byte count, and SHA-256 checks.
It streams into a private temporary file in the target directory and publishes
the final name atomically after verification. Ctrl+C and SIGTERM remove the
temporary file. No audio passes through this script.

The default file is
[ggml-small.en-q5_1.bin at the pinned revision](https://huggingface.co/ggerganov/whisper.cpp/blob/98aa99a0a9db05ae2342309f5096248665f7cba3/ggml-small.en-q5_1.bin):

```text
Revision: 98aa99a0a9db05ae2342309f5096248665f7cba3
Bytes:    190098681
SHA-256:  bfdff4894dcb76bbf647d56263ea2a96645423f1669176f4844a1bf8e478ad30
```

The Medium English download uses the same immutable revision:

| Model | Bytes | SHA-256 |
| --- | ---: | --- |
| `medium.en-q5_0` | 539225533 | `76733e26ad8fe1c7a5bf7531a9d41917b2adc0f20f2e4f5531688a8c6cd88eb0` |

Whisper's original code and weights are released under the
[MIT license](https://github.com/openai/whisper#license).
