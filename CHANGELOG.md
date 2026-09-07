# Changelog

## 0.6.0

- Reuse model weights for 60 seconds, with fresh decoder state per recording.
  Release weights on expiry, model changes, and exit.
- Compare Base and Small and evaluate vocabulary prompting. Defer hints because
  they introduce recognition regressions; keep the preferences unchanged.
- Make the offline transcription command English-only, matching the app.
- Shorten the documentation, remove the README image and redundant extension
  guide, and share the model list between settings validation and preferences.

## 0.5.1

- Reject IBus's unfocused fallback context before starting live dictation.
- Restore the compact, centered preferences layout with Model, Start, Finish,
  and Live text controls.
- Keep recording bars visible with cached or current styles, with rounded ends
  at every volume; show three bouncing dots while finalizing transcription.
- Request muted green inline drafts without underlining where the destination
  application's input method supports those attributes.
- Preserve the release packaging, migration behavior, and focused test suite.
- Remove local checkout paths from compiled diagnostic strings and reject
  release executables containing the builder's home path.
- Add the project roadmap and repository links; pin CI actions and complete
  unattended test dependency installation.

## 0.5.0

- Introduce lilt, with local CPU dictation and GNOME 46 integration.
- Provide live composition, configurable Start/Finish keys, and English models.
- Preserve configuration and models from the PTT prototype.
- Fix stale D-Bus callbacks and IBus cleanup during extension reloads.
- Provide portable native, extension, and matching source release archives.
- Include focused core tests and one isolated desktop integration harness.
