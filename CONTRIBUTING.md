# Contributing

Build dependencies and user setup are in the [README](README.md). Keep audio and
recognition in the native application and desktop integration in the extension.
The [extension reference](extension/README.md) describes the D-Bus contract.
Use the [project repository](https://github.com/w4term3loon/lilt) for changes and
reports, and the [roadmap](docs/roadmap.md) for planned work and acceptance criteria.
Build from a clean clone using relative project paths; keep private recordings,
settings, model weights, and diagnostic captures outside version control.

## Tests

`./scripts/build.sh` builds the application and runs CTest. With the README's test
dependencies installed, the core suite covers:

- Text/Unicode handling, WAV validation, inference queues, and cancellation.
- Legacy configuration/model migration without overwriting user data.
- Native D-Bus startup, restart, trusted attachment, and error handling.
- Shortcut matching, held keys, stale replies, and extension cleanup.
- Installation, upgrades, uninstallation, quoting, and staging containment.

The single desktop harness runs against real GNOME/GTK input services:

```bash
for mode in wayland ibus x11; do
    bash extension/tests/headless-smoke.sh "$mode"
done
```

It checks draft revision, final insertion, no unintended submission, cancellation,
focus loss, and input-method restoration. All integration fixtures use private
D-Bus/XDG/display sessions and synthetic recognition. A passing harness does not
establish compatibility with every application or a transcription accuracy score.

After building release artifacts, verify their contents and extracted installer:

```bash
python3 scripts/package.py --output dist
python3 tests/test_packaging.py --archives dist
```

CI runs these core, desktop, and archive checks. For changes, describe the problem,
resulting behavior, and relevant verification. Add focused regressions for real
bugs; avoid tests of unused code and duplicated integration scenarios.

Inspect recording bars at low and high volume and the three finalization dots
when changing indicator styling. Check preferences in light and dark themes.
Inline draft appearance needs a real destination-field check: GTK IBus honors
the green foreground and disabled underline, while other input paths may impose
their own composition style.

## Installation paths

The installer defaults to `~/.local`, respecting `XDG_DATA_HOME`. For staging:

```bash
python3 scripts/install.py --prefix /usr --data-dir /usr/share --destdir /tmp/lilt-stage
```

`--destdir` redirects filesystem writes and never contacts the active desktop.
`--no-enable` installs files without modifying extension state. A custom live
installation's data directory must be discoverable through GNOME/D-Bus's XDG
paths; changing the executable prefix alone does not configure discovery.

The installed uninstaller is `PREFIX/share/lilt/uninstall.py`. It remembers the
installation layout. Models/configuration use runtime XDG directories separately;
`--user-data-dir` and `--config-dir` select their bases when using `--purge-data`.

## License

Contributions use GPL-3.0-or-later. Preserve notices for incorporated third-party
code. Only add GNOME versions to metadata after testing them.
