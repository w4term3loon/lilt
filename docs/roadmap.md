# Roadmap

Keep lilt a small, local English dictation tool.

## Done in 0.6

- [x] Cache selected model weights for 60 seconds, with fresh decoder state per
  session. Verify reuse, expiry, model changes, cancellation, and overlap rejection.
- [x] Evaluate optional vocabulary prompting; leave it out after recognition regressions.
- [x] Compare Base and Small on public speech. Keep Small as the default.
- [x] Remove the README image and redundant extension guide; consolidate model IDs.

## Next

- [ ] Compare representative personal dictation: corrections, pauses, noise,
  accents, and technical terms. Use corrected references and retain no recordings.
- [ ] Measure speech-to-visible-draft and Finish-to-committed-text latency in
  real destination applications, including Codex.
- [ ] Revisit vocabulary only if it improves unseen names without introducing
  errors in ordinary or ambiguous speech.
- [ ] Improve speech/noise discrimination: the noise control produced text even
  without prompting. Digital silence remained empty.

Learning, camera input, and MCP integrations remain deferred. No ambient recording
or automatic training-data collection is planned for the core app.

## Preliminary comparison · 2026-09-07/08

Twelve public audiobook clips, 92.38 seconds and 217 reference words, from one
speaker. This is a small convenience sample, not spontaneous dictation or an
independent assessment of general accuracy. Known benchmark data may overlap training.

| English Q5_1 model | Cold seconds, median (range) | Warm seconds, median (range) | Word errors | Peak RAM range |
| --- | --- | --- | --- | --- |
| Base | 1.27 (0.85–1.97) | 1.26 (0.81–1.93) | 31/217 · 14.3% | 152–170 MiB |
| Small | 4.32 (3.78–6.04) | 4.08 (3.74–5.89) | 15/217 · 6.9% | 346–363 MiB |

Small made fewer word errors; Base was faster and used less RAM. Eleven of twelve
Base clips and ten Small clips needed a word correction. Annotated name errors
were 7/11 and 5/11 occurrences respectively (exact aligned words, ignoring possessive
suffixes). Punctuation was not scored: official
references are unpunctuated. WER lowercases text, removes ASCII punctuation except
internal apostrophes, splits on whitespace, and counts word substitutions,
deletions, and insertions. Honorifics and number spellings are not expanded.

Each model had two passes, ordered Base/Small then Small/Base. Each clip was
decoded after releasing weights, then immediately again with cached weights:
24 timings per model/state. "Cold" does not mean an empty operating-system disk
cache. All four hypotheses per clip were identical. Median paired savings were
0.040 seconds (3.3%) for Base and 0.226 seconds (5.5%) for Small; individual pairs
varied from slower to faster. These are modest offline savings, not a promise of
faster live finalization.

Measurements use the engine from [82014ba](https://github.com/w4term3loon/lilt/tree/82014ba),
Ubuntu 24.04, Intel Core Ultra 7 268V, four inference threads, GCC 13.3 Release,
and `GGML_NATIVE=ON`. Release archives use portable CPU settings and will differ.
Both models use the same English greedy decoder and filters. Wall time brackets
the complete offline WAV call with `steady_clock`; RAM uses Linux `VmRSS` and
`VmHWM`, resetting the latter before each call. These are engine-process figures,
not GTK or GNOME totals. Desktop load and CPU frequency were not controlled.

Final live checks use [115c9ef](https://github.com/w4term3loon/lilt/tree/115c9ef),
which returns unused allocator pages at idle. Two clips (5.855 and 15.115 seconds)
were replayed cold and warm through a private PulseAudio null sink; no microphone
was used. Draft timing starts at the first active audio-level callback (roughly
100 ms resolution); Finish timing ends at engine idle. These exclude cursor
rendering and insertion. Each table cell is median (range), in seconds, n=2.

| Model/state | First draft | Finish |
| --- | --- | --- |
| Base cold | 1.70 (1.19–2.22) | 0.73 (0.68–0.78) |
| Base warm | 1.05 (0.79–1.31) | 0.76 (0.65–0.86) |
| Small cold | 3.65 (3.25–4.04) | 5.38 (4.41–6.36) |
| Small warm | 3.68 (3.14–4.23) | 7.34 (6.57–8.10) |

All eight sessions produced drafts and closed their capture stream before idle.
Small's warm Finish was slower here; this tiny replay sample cannot establish a
live speed benefit. After four sessions, idle RSS was 104 MiB (Base) / 236 MiB
(Small); clearing weights reduced it to 39 / 47 MiB. Live peaks were 156–191 /
354–379 MiB. A separate real Base expiry test fell from 76 to 11 MiB after
60 seconds. Allocator/thread runtime retention means RSS need not return to its
initial value. The earlier untrimmed Base replay reached 344 MiB at idle.

Vocabulary prompting was rejected for this release. Nine names drawn from the
references reduced name errors to 5/11 (Base) and 2/11 (Small), but each model also
made two clips worse. Three unrelated computing terms increased WER to 15.7%
and 12.4%, respectively; Small introduced word errors in a previously correct
clip. This one-pass trial used the same audio and decoder. Reference-derived
hints are an optimistic test, not evidence of learning. Three-second digital
silence and seeded white noise were also tested with both hint lists and without
hints: silence stayed empty, but noise produced spurious text in every condition.

Audio comes from the [pinned Hugging Face excerpt](https://huggingface.co/datasets/hf-internal-testing/librispeech_asr_dummy/blob/7d6b249bbe2b91e6253e2904d23a659994eb5e8e/librispeech_asr_dummy.py)
of [LibriSpeech](https://www.openslr.org/12), by Panayotov, Chen, Povey, and Khudanpur,
under CC BY 4.0. FLAC was losslessly decoded to 16 kHz mono PCM16 WAV. Selection
preceded inference: speaker 1272, chapter 128104 clips 0000/0003/0007/0011;
135031 clips 0000/0005/0015/0023; 141231 clips 0000/0006/0026/0028.
Only aggregates and methodology belong in Git; temporary audio and text are removed.

Pinned SHA-256 values (model publisher revision is linked in the README):

```text
Base:    4baf70dd0d7c4247ba2b81fafd9c01005ac77c2f9ef064e00dcf195d0e2fdd2f
Small:   bfdff4894dcb76bbf647d56263ea2a96645423f1669176f4844a1bf8e478ad30
Archive: 663413297096f851df4ec4023fafbf7e2c6b3eccbed364741a82e457792be7de
```
