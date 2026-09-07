# Changelog

## 0.5.0 — 2026-09-07

- Adopt the name **lilt** across the application, extension, command line,
  D-Bus interface, settings, and documentation.
- Preserve settings and model files from the earlier PTT prototype without
  overwriting an existing lilt configuration or deleting the old files.
- Fix stale D-Bus callbacks when the extension is disabled and enabled again.
- Remove the production extension's Gdk dependency.
- Prepare portable CPU builds, standalone installation archives, extension ZIPs,
  checksums, and automated build/package checks.
- Add GPL-3.0-or-later licensing and third-party notices.

## 0.4.0 — prototype

- Live input-method composition in the destination field, with cancel and
  explicit final insertion.
- English Tiny, Base, Small, and Medium Whisper model choices.
- Configurable Start and Finish shortcuts, a recording indicator, and local
  CPU transcription.

Earlier development measurements and compatibility results are recorded in
[docs/validation.md](docs/validation.md).
