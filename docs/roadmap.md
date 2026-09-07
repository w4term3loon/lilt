# Roadmap

Keep lilt a minimal English dictation tool. These are acceptance criteria for
future work, not claims that the features exist. Check items after verification.

## Current release and publication

- [x] Verify the 0.5.1 preferences layout, rounded recording bars, cached-style
  compatibility, bouncing finalization dots, and green inline drafts. Preserve
  final insertion without submission and cancellation cleanup. Check the real
  render as well as the focused test suite.
- [x] Review the complete tracked content and history before publication. Keep
  private recordings, transcripts, settings, credentials, and machine-specific
  paths out of Git. Include only deliberately prepared synthetic documentation
  images; exclude build products and model files from the source repository.
- [ ] Publish to [w4term3loon/lilt](https://github.com/w4term3loon/lilt), verify the
  pushed revision, and confirm the first hosted CI run and its release artifacts.

## 1. Compare Base and Small on representative dictation

- [ ] Define a fixed private English sample set covering short and long
  dictations, pauses, corrections, names, and technical terms. Record sample
  counts and durations; prepare corrected reference text before comparing
  models. Use the same audio for both models.
- [ ] Compare Base English Q5_1 and Small English Q5_1 with identical decoder
  settings, thread counts, and hardware. Record app/engine revisions and model
  hashes. Separate cold and warm runs; repeat runs and alternate model order.
- [ ] Measure first-draft latency from speech onset and finalization latency
  from Finish to committed text. Report medians and ranges, peak process RAM,
  and idle RAM. State how timestamps and memory were collected.
- [ ] Report word error rate with an explicit normalization rule, plus the
  number of dictations needing correction and errors in names/terms. Keep
  punctuation differences separate. Report sample counts and limitations
  rather than claiming a universal accuracy ranking.
- [ ] Keep audio and reference text local and outside Git. Publish anonymous
  aggregate results only; delete temporary benchmark material after comparison.
  Change the default only if the measured tradeoff justifies it.

## 2. Evaluate a short model warm cache

- [ ] Retain only the selected model context between sessions for a documented,
  short idle timeout. Starting another dictation reuses it; expiry, model
  changes, and application exit release it safely.
- [ ] Retain no audio, transcript, or previous-session decoder state in the
  cache. The microphone closes at the end of recording and stays closed while
  the model is idle.
- [ ] Compare repeat-dictation latency and idle/peak RAM against no cache.
  Verify expiry, cancellation, model changes, and repeated sessions without
  concurrent decoder access, memory growth, or stale text. Document any memory
  the allocator retains after model release.

## 3. Evaluate optional local vocabulary hints

- [ ] Accept a small, explicitly supplied list of names and terms stored
  locally. Feed it through Whisper's existing decoder prompting; do not train
  or modify weights. Keep the feature off unless configured.
- [ ] Compare identical recordings with hints enabled and disabled. Measure
  recognition of intended terms and regressions on unrelated speech, silence,
  and ambiguous audio; reject changes that encourage invented words.
- [ ] Bound hint length, provide a clear way to remove hints, and keep private
  vocabulary out of logs and Git. Add a preference only if the measured benefit
  warrants the extra UI.

## Deferred

- [ ] Correction-based local learning remains a separate experiment. No
  background training, ambient recording, or automatic training-data collection.
- [ ] Camera input and MCP integrations remain outside the current scope.
  Revisit them after dictation latency, accuracy, and input-field behavior are
  reliable, without expanding the core preferences unnecessarily.
