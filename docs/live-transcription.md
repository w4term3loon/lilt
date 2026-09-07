# Live transcription and model research

Investigated 7 September 2026 on Ubuntu 24.04 / GNOME 46 Wayland, Intel Core
Ultra 7 268V, 32 GB RAM. This document distinguishes implemented behavior from
experiments. Version 0.4.0 replaces the separate preview bubble with input-method
composition inside the focused field. The versioned investigations below record
the earlier designs and their limits.

## In-field composition (0.4.0)

The extension registers a temporary IBus engine over its own private connection.
It keeps the original input context focused, sends replaceable preedit text,
and commits one final transcript after Finish and all held-key releases. Escape
clears the composition without committing it. The engine uses preedit focus mode
CLEAR, so losing focus does not accidentally commit an unfinished draft.

The previous global input engine is restored after cleanup if the temporary
dictation engine is still selected. A deliberate user input-source change takes
precedence. Registration, switching, and restoration are asynchronous: the
engine factory runs in GNOME's main loop and cannot service requests while that
same loop is blocked by a synchronous switch. The extension never destroys the
IBus.Bus singleton or closes GNOME's shared connection.

IBus clients can render per-span gray tentative text and normal stable text.
GNOME 46's native Wayland path forwards the preedit string but does not retain
these foreground attributes; it uses the client's composition styling. The
field's input protocol therefore determines color/underline behavior. The
separate bubble and its positioning code are removed from the active extension.

Preferences is limited to Model, Start, Finish, and Live text, with additional
messages only for actionable errors/setup. Transcription is fixed to English;
legacy model choices migrate to the English variant of the same size. Training
remains deferred. The project adopts the name lilt in version 0.5.0; the remote
repository location remains undecided.

## Implemented live preview (0.3.0)

The engine loads one Whisper context while capture continues, then serially
decodes growing audio snapshots. Snapshot publication and inference starts are
limited to at most once every two seconds; one pending snapshot is replaced by
the newest audio while the decoder is busy. This bounds the work queue and audio
memory, but repeated full-prefix inference becomes slower for longer dictations.
The existing 180-second recording limit remains. Finish stops pending partial
work and performs one final decode; Escape closes capture and suppresses results.
Whisper may take time to reach an encoder cancellation boundary, so a partial
pass can add to the delay after Finish. Disabling preview avoids that extra work.

The extension receives UTF-8 text and a stable-prefix byte count over D-Bus.
Completed words shared by consecutive hypotheses use the preview theme's normal
foreground color; the changing tail is gray. Stability means agreement, not
verified correctness. A later correction can shorten the stable prefix. Text
without space-delimited words remains tentative until the final result.

The preview is a small noninteractive Shell bubble, showing at most the last
160 Unicode code points. It uses caret geometry captured before the modal grab,
or the original window's lower edge if the application does not expose it, and
stays within the monitor work area. It supports the desktop light/dark preference.
The existing bottom waveform and keyboard grab remain. No provisional text is
committed, so Escape can still discard the entire session. The native Wayland
preedit experiments below remain separate and are not installed.

Preferences has one **Live preview** switch, enabled by default. Turning it off
keeps model preloading and final-only recognition. No training or recording
retention is enabled; the user deferred learning to a later experiment.

Medium Q5_0 (769 million parameters) is available and its multilingual weights
were downloaded and verified. Existing Base English selection was preserved.
On the same 11-second JFK fixture, separate CLI runs took 1.319 seconds for
Base English Q5_1 (162,796 KiB peak RSS) and 9.899 seconds for Medium Q5_0
(847,168 KiB peak RSS). Both recognized the words correctly, with different
punctuation. These are single CPU runs including model loading, not live-update
latencies or an accuracy comparison across languages and accents.

## Model versus inference engine

Whisper is a family of trained speech-recognition models. whisper.cpp implements
their architecture, numerical operations, decoding, and hardware backends in
C/C++. It runs compatible Whisper weights, not arbitrary public audio models.
Another model architecture generally needs another backend and integration.

Representative public models, not an exhaustive size or accuracy ranking:

| Model | Parameters | Relevant distinction |
| --- | ---: | --- |
| Whisper Small / Medium (supported in app) | 244 / 769 million | Multilingual models available as quantized single-file weights. |
| Whisper Large-v3 / Turbo | 1.55 billion / 809 million | Larger multilingual Whisper models; Turbo reduces decoder cost. |
| Qwen3-ASR | 0.6 / 1.7 billion | Offline and streaming design; 30 languages including Hungarian. Official streaming implementation currently uses vLLM. |
| Parakeet-TDT 0.6B v3 | 0.6 billion | 25 European languages including Hungarian. Its documented chunked-streaming example uses 2-second chunks and 2 seconds of right context. |
| Canary-Qwen | 2.5 billion | English transcription and language-model capabilities. |
| Voxtral Mini Realtime | 4 billion | Dedicated streaming transcription in 13 languages; Hungarian is not among them. Recommended 480 ms lookahead is not a measured end-to-end laptop latency. |
| Step-Audio-Chat | approximately 130 billion | A much broader audio-language system, not a practical model for this minimal laptop application. |

Primary sources: [Whisper](https://github.com/openai/whisper),
[Qwen3-ASR](https://huggingface.co/Qwen/Qwen3-ASR-1.7B),
[Parakeet](https://huggingface.co/nvidia/parakeet-tdt-0.6b-v3),
[Canary-Qwen](https://huggingface.co/nvidia/canary-qwen-2.5b),
[Voxtral Realtime](https://huggingface.co/mistralai/Voxtral-Mini-4B-Realtime-2602),
[Step-Audio](https://github.com/stepfun-ai/Step-Audio).
Model weight size, total/active parameter count, runtime memory, language coverage,
and latency are different measures. Vendor benchmarks use different datasets and
hardware and do not establish a fair ranking for this computer.

## Training feasibility

Specializing an existing model for a particular language, microphone, vocabulary,
or speaking style is a realistic experiment. It requires clean paired recordings
and transcripts plus a held-out evaluation set. Improvement on the training
recordings is not evidence of improvement on new dictation.

For scale, Hugging Face's reproducible Whisper Small tutorial uses 8 hours of
Hindi training data and reports approximately 5–10 GPU hours. Those are the
tutorial's conditions, not a resource estimate or expected accuracy improvement
for this user. [Fine-tuning example](https://huggingface.co/blog/fine-tune-whisper)

Training a broadly competitive model is a different scale: Canary-Qwen reports
234,000 audio hours and 90,000 training steps using 32 A100 80 GB GPUs, even with
a frozen language-model backbone and adaptation of selected components. Whisper
Large-v3 reports 1 million weakly labelled plus 4 million pseudo-labelled audio
hours. [Canary training](https://huggingface.co/nvidia/canary-qwen-2.5b),
[Whisper Large-v3](https://huggingface.co/openai/whisper-large-v3)

Distillation trains a smaller student using a larger teacher. The original
Distil-Whisper work used 22,000 hours; it is not equivalent to a small personal
fine-tune. [Distil-Whisper](https://github.com/huggingface/distil-whisper)

## Background model loading

Version 0.2.1 starts model initialization after the microphone stream is ready,
on a separate thread. Capture continues immediately, and final decoding reuses
that context. The model is released at the end of the session; there is no model
loaded at login or permanently retained while idle. A silent online recording
can still initialize the model; silence is excluded from decoding. The offline
transcription entry point retains its no-model-load silence fast path.

This overlaps initialization with speaking. It does not accelerate the neural
network itself. Whisper initialization has no cancellation hook: cancellation
closes capture promptly, but the worker must finish initialization and cleanup
before another session starts.

## Repeated-prefix inference experiment

A standalone probe loaded the installed Small Q5_1 model once and decoded the
first 2, 4, 6, 8, 10, and 11 seconds of whisper.cpp's JFK fixture. Settings matched
the app's four-thread CPU decoder, with English selected, greedy decoding,
best-of-one, no context, and default audio context. This was an offline prefix
experiment, not a live microphone test or a production streaming algorithm.

| Available audio | Decode duration |
| ---: | ---: |
| 2 seconds | 2.865 seconds |
| 4 seconds | 2.842 seconds |
| 6 seconds | 3.125 seconds |
| 8 seconds | 3.916 seconds |
| 10 seconds | 5.055 seconds |
| 11 seconds | 4.646 seconds |

Model initialization took 0.398 seconds. Peak RSS was 355,560 KiB (about 347 MiB).
These six decoding passes took 22.87 seconds total including initialization.
Timing varies with machine load and power conditions. This famous, clean English
fixture is not representative evidence of personal dictation accuracy.

An actual hypothesis revision illustrates the UI problem: the 6-second result
ended with “what your country is.”; with more audio it became “what your country
can do for you.” Punctuation also changed. Blindly appending each result produces
duplicates; deleting and retyping around the cursor risks modifying user text.

The measured Small decoder cannot sustain a new complete-prefix decode every
2 seconds. Version 0.3.0 coalesces pending updates, caps retained audio, and
distinguishes tentative and stable text. It still reprocesses the full prefix;
chunk-boundary handling is not yet implemented. Faster
streaming models/backends or decoder optimizations merit separate benchmarks.
Time to first visible text and time to stable text are separate from throughput.

Whisper was not designed as an intrinsically streaming recognizer. Research such
as LocalAgreement checks agreement between successive hypotheses before committing
text; the published latency is measured under that paper's conditions, not ours.
[Whisper-Streaming paper](https://arxiv.org/abs/2307.14743),
[whisper.cpp streaming example](https://github.com/ggml-org/whisper.cpp/tree/master/examples/stream)

## Text at the cursor

The desired behavior is editable composition (IME preedit): provisional words
appear at the caret, may be revised, and are committed once on Finish. Escape
clears the composition. Committing stable fragments immediately would change
Escape's existing discard-all behavior, unless the destination provided a reliable
transaction/undo interface.

In an isolated GNOME 46 session with a real GTK Wayland entry, the native preedit
API displayed and revised provisional text while the entry's committed text
remained empty. Clearing the preedit and committing a final Unicode sentence
worked without activating/submitting the entry. During the app's current modal
Shell grab, however, the input-method focus was absent and preedit updates were
dropped. Therefore live composition requires changes to keyboard/focus handling;
adding a partial-transcript D-Bus signal alone is insufficient.

The existing IBus panel path provides final text commits but does not expose an
equivalent outgoing preedit-update method. Native Wayland GTK evidence does not
establish compatibility with Electron/Codex, browsers, XWayland, or every widget.
A proper input-method integration or a capability-tested desktop path is needed.

A second isolated prototype intercepted keys through GNOME's input-method filter
while leaving the application focused. It displayed and revised preedit,
consumed Ctrl+F8, autorepeat, held Enter, and modifier releases, and waited for
all held keys to be released before a single commit. Escape discarded the next
composition. The final committed string was `Final speech 🦉 More final.` and
the entry's activation count stayed at zero. These were synthetic recognition
updates and synthetic key events, not live speech recognition.

![Provisional text in the isolated GTK test entry](live-preedit.png)

The simpler alternative of listening for Shell stage key events was a useful
negative control: it missed application-directed keys and Return activated the
entry. The successful filter prototype hooks private GNOME methods and is not
installed. A separate GTK IBus-client probe did not receive its preedit updates.
Existing selections/compositions, focus changes, GTK4, Qt, Electron/Codex,
XWayland, and an entire audio session still require validation.

Any production implementation must exercise: held Finish modifiers and releases,
no Return activation, late results after cancellation, target changes/closure,
selection replacement, non-ASCII text, composition cleanup, and applications
without preedit support. The tiny bottom indicator can stay unchanged.

References: [GNOME 46 input method](https://github.com/GNOME/gnome-shell/blob/46.0/js/misc/inputMethod.js),
[GNOME modal handling](https://github.com/GNOME/gnome-shell/blob/46.0/js/ui/main.js),
[Mutter preedit delivery](https://github.com/GNOME/mutter/blob/46.0/src/wayland/meta-wayland-text-input.c),
[IBus engine composition](https://ibus.github.io/docs/ibus-1.5/IBusEngine.html#ibus-engine-update-preedit-text-with-mode).
