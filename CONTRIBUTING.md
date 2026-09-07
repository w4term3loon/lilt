# Contributing to lilt

lilt targets Ubuntu 24.04, GNOME Shell 46, and English dictation. Keep recognition
and audio capture in the native application; the extension handles desktop UI,
shortcuts, and input-method integration.

Read the [README](README.md) for build dependencies and setup. Build and run the
native tests with `./scripts/build.sh`. Run the extension text/session tests and
the isolated GNOME integration tests listed in [extension/README.md](extension/README.md).
Tests must use private D-Bus, XDG, and display sessions: do not record a real
microphone or modify the contributor's active desktop without their consent.

For a bug report, include the lilt version, Ubuntu and GNOME Shell versions,
Wayland/X11 session type, affected application, selected model, and steps to
reproduce. Remove private text from logs and screenshots. The existing
[validation notes](docs/validation.md) distinguish synthetic integration tests
from actual recognition measurements.

For changes, explain the user-visible problem, the resulting behavior, and the
checks performed. Add regressions for lifecycle, input handling, data migration,
and packaging bugs. Update supported GNOME versions only after testing them.

Contributions are provided under GPL-3.0-or-later, the project's license.
Keep upstream attribution when incorporating third-party code.
