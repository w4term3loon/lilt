#!/usr/bin/env python3
"""Install lilt and its matching GNOME extension for one user, without sudo."""

import argparse
import ast
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

from install_support import (ROOT, add_layout_arguments, best_effort, copy_extension,
                             copy_file, desktop_quote, extension_metadata,
                             layout_from_args, refresh_desktop, write_text)


def enable_extension(uuid):
    # The old extension must not compete for the shortcut after the rename.
    best_effort(['gnome-extensions', 'disable', 'ptt@local'])
    if best_effort(['gnome-extensions', 'enable', uuid]):
        print('Extension enabled. Log out and back in to load updated modules after an upgrade.')
        return
    if shutil.which('gsettings'):
        try:
            current = subprocess.check_output(
                ['gsettings', 'get', 'org.gnome.shell', 'enabled-extensions'], text=True, timeout=5).strip()
            if current.startswith('@as '):
                current = current[4:]
            entries = ast.literal_eval(current)
            if not isinstance(entries, list) or not all(isinstance(value, str) for value in entries):
                raise ValueError('Unexpected enabled-extensions value')
            entries = [entry for entry in entries if entry != 'ptt@local']
            if uuid not in entries:
                entries.append(uuid)
            subprocess.run(['gsettings', 'set', 'org.gnome.shell', 'enabled-extensions', repr(entries)],
                           check=True, timeout=5)
        except (subprocess.SubprocessError, ValueError, SyntaxError):
            print('Enable lilt manually in the Extensions app after logging back in.')
    print('Log out and back in once so GNOME discovers the extension.')


def install(args):
    layout = layout_from_args(args)
    binary = args.binary or next((path for path in (ROOT / 'bin/lilt', ROOT / 'build/lilt')
                                if path.is_file()), ROOT / 'build/lilt')
    if not binary.is_file():
        raise ValueError('Build first: ./scripts/build.sh (or use --binary PATH).')
    if not shutil.which('glib-compile-schemas'):
        raise ValueError('glib-compile-schemas is required (Ubuntu package: libglib2.0-bin).')
    metadata = extension_metadata()
    uuid = metadata['uuid']
    # Validate all required payload files before changing an installed executable.
    required = [ROOT / 'LICENSE', ROOT / 'THIRD_PARTY_NOTICES.md',
                ROOT / 'LICENSES/whisper.cpp-MIT.txt', ROOT / 'data/io.github.lilt.Dictation.svg',
                *(ROOT / 'scripts' / name for name in ('download-model.py', 'uninstall.py', 'install_support.py'))]
    for source in required:
        if not source.is_file():
            raise ValueError('Incomplete lilt installation payload: ' + str(source))
    extension_target = layout.staged(layout.data / 'gnome-shell/extensions' / uuid)
    extension_target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.lilt-', dir=extension_target.parent) as work:
        prepared = Path(work) / uuid
        copy_extension(prepared)
        copy_file(binary, layout.staged(layout.binary), 0o755)
        for name in ('download-model.py', 'uninstall.py', 'install_support.py'):
            copy_file(ROOT / 'scripts' / name, layout.staged(layout.assets / name), 0o755)
        for name in ('LICENSE', 'THIRD_PARTY_NOTICES.md'):
            copy_file(ROOT / name, layout.staged(layout.assets / name))
        copy_file(ROOT / 'LICENSES/whisper.cpp-MIT.txt', layout.staged(layout.assets / 'LICENSES/whisper.cpp-MIT.txt'))
        write_text(layout.staged(layout.assets / 'install.json'), json.dumps(
            {'prefix': str(layout.prefix), 'data': str(layout.data), 'uuid': uuid}, indent=2) + '\n')
        copy_file(ROOT / 'data/io.github.lilt.Dictation.svg',
                  layout.staged(layout.data / 'icons/hicolor/scalable/apps/io.github.lilt.Dictation.svg'))
        # Replace the runtime directory as a unit, removing stale modules while
        # keeping the prior installation intact until schema compilation passes.
        backup = Path(work) / 'previous'
        if extension_target.exists() or extension_target.is_symlink():
            extension_target.rename(backup)
        try:
            prepared.rename(extension_target)
        except OSError:
            if backup.exists() or backup.is_symlink():
                backup.rename(extension_target)
            raise
    launch = desktop_quote(str(layout.binary))
    applications = layout.staged(layout.data / 'applications')
    applications.mkdir(parents=True, exist_ok=True)
    write_text(layout.staged(layout.data / 'applications/io.github.lilt.Dictation.desktop'),
        '[Desktop Entry]\nType=Application\nName=lilt\nComment=Local voice typing\n'
        f'Exec={launch}\nIcon=io.github.lilt.Dictation\nTerminal=false\nCategories=Utility;Accessibility;\n'
        'StartupNotify=true\n')
    services = layout.staged(layout.data / 'dbus-1/services')
    services.mkdir(parents=True, exist_ok=True)
    service_launch = shlex.quote(str(layout.binary)).replace('\\', '\\\\')
    write_text(layout.staged(layout.data / 'dbus-1/services/io.github.lilt.Dictation.service'),
        '[D-BUS Service]\nName=io.github.lilt.Dictation\n'
        f'Exec={service_launch} --daemon\n')
    if not args.destdir and not args.no_enable:
        refresh_desktop(layout)
        enable_extension(uuid)
    print(('Staged' if args.destdir else 'Installed') + ' lilt: ' + str(layout.staged(layout.binary)))
    print('Model: python3 ' + str(layout.assets / 'download-model.py'))
    print('Remove: python3 ' + str(layout.assets / 'uninstall.py'))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    add_layout_arguments(parser)
    parser.add_argument('--binary', type=Path, help='executable to install (default: bin/lilt or build/lilt)')
    args = parser.parse_args(argv)
    try:
        install(args)
    except (OSError, ValueError, argparse.ArgumentTypeError, subprocess.SubprocessError) as error:
        parser.exit(1, 'Installation failed: ' + str(error) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
