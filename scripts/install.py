#!/usr/bin/env python3
"""Install ren for the current user, without sudo."""

import argparse
import ast
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
PREFIX = Path.home() / '.local'
DATA = Path(os.environ.get('XDG_DATA_HOME') or PREFIX / 'share')


def run(*command):
    try:
        return subprocess.run(command, check=False, capture_output=True, timeout=5).returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


def copy(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=target.parent, delete=False) as temporary:
        staged = Path(temporary.name)
    try:
        shutil.copy2(source, staged)
        staged.replace(target)
    finally:
        staged.unlink(missing_ok=True)


def install(binary):
    if not binary.is_file():
        raise ValueError('Build first: ./scripts/build.sh')
    if not DATA.is_absolute():
        raise ValueError('XDG_DATA_HOME must be an absolute path.')
    # Desktop Entry forbids '=' in executables; GLib cannot discover one with '%'.
    if any(character in str(PREFIX) for character in '%=\r\n'):
        raise ValueError('The home path must not contain %, = or line breaks.')
    uuid = json.loads((ROOT / 'extension/metadata.json').read_text())['uuid']
    extensions = DATA / 'gnome-shell/extensions'
    extensions.mkdir(parents=True, exist_ok=True)
    target = extensions / uuid
    with tempfile.TemporaryDirectory(prefix='.ren-', dir=extensions) as temporary:
        prepared = Path(temporary) / uuid
        prepared.mkdir()
        files = [*ROOT.glob('extension/*.js'), *ROOT.glob('extension/*.svg'), ROOT / 'extension/metadata.json',
                 ROOT / 'extension/stylesheet.css', *ROOT.glob('extension/schemas/*.xml')]
        for source in files:
            copy(source, prepared / source.relative_to(ROOT / 'extension'))
        copy(ROOT / 'LICENSE', prepared / 'LICENSE')
        subprocess.run(['glib-compile-schemas', '--strict', str(prepared / 'schemas')], check=True)
        copy(binary, PREFIX / 'bin/ren')
        for name in ('download-model.py', 'uninstall.py'):
            copy(ROOT / 'scripts' / name, PREFIX / 'share/ren' / name)
        for name in ('LICENSE', 'THIRD_PARTY_NOTICES.md', 'LICENSES/whisper.cpp-MIT.txt'):
            copy(ROOT / name, PREFIX / 'share/ren' / name)
        previous = Path(temporary) / 'previous'
        if target.exists() or target.is_symlink():
            target.rename(previous)
        try:
            prepared.rename(target)
        except OSError:
            if previous.exists() or previous.is_symlink():
                previous.rename(target)
            raise

    alias = PREFIX / 'bin/youlilt'
    if alias.is_symlink() and alias.resolve() in (PREFIX / 'bin/lilt', PREFIX / 'bin/ren'):
        alias.unlink()
    # Retire old launchers together; stale bus names cause activation timeouts.
    for old in ('ren', 'lilt', 'ptt'):
        run('gnome-extensions', 'disable', f'{old}@local')
        legacy = extensions / f'{old}@local'
        if legacy.is_symlink():
            legacy.unlink()
        elif legacy.exists():
            shutil.rmtree(legacy)
        if old == 'ren':
            continue
        old_binary = PREFIX / 'bin' / old
        if old_binary.exists():
            run(str(old_binary), '--quit')
        old_binary.unlink(missing_ok=True)
        for directory, suffix in (('dbus-1/services', 'service'), ('applications', 'desktop'),
                                  ('icons/hicolor/scalable/apps', 'svg')):
            (DATA / directory / f'io.github.{old}.Dictation.{suffix}').unlink(missing_ok=True)
    # Keep pinned launcher positions when replacing the application identity.
    try:
        favorites = ast.literal_eval(subprocess.check_output(
            ['gsettings', 'get', 'org.gnome.shell', 'favorite-apps'], text=True, timeout=5).strip().removeprefix('@as '))
        updated = list(dict.fromkeys('io.github.ren.Dictation.desktop' if entry in
            ('io.github.lilt.Dictation.desktop', 'io.github.ptt.Dictation.desktop') else entry for entry in favorites))
        if updated != favorites:
            run('gsettings', 'set', 'org.gnome.shell', 'favorite-apps', repr(updated))
    except (OSError, ValueError, SyntaxError, subprocess.SubprocessError):
        pass
    # Legacy settings and models remain available for the native migration.

    binary = PREFIX / 'bin/ren'
    # Desktop Exec and D-Bus Exec use different quoting rules.
    quoted = str(binary).replace('\\', '\\\\').replace('"', '\\"').replace('`', '\\`').replace('$', '\\$').replace('%', '%%')
    quoted = '"' + quoted.replace('\\', '\\\\') + '"'
    service_command = shlex.quote(str(binary)).replace('\\', '\\\\')
    registrations = {
        'applications/io.github.ren.Dictation.desktop':
            '[Desktop Entry]\nType=Application\nName=Ren\n'
            f'Exec={quoted}\nIcon=io.github.ren.Dictation\nTerminal=false\nCategories=Utility;Accessibility;\n',
        'dbus-1/services/io.github.ren.Dictation.service':
            '[D-BUS Service]\nName=io.github.ren.Dictation\n'
            f'Exec={service_command} --daemon\n',
    }
    for name, content in registrations.items():
        path = DATA / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
    copy(ROOT / 'data/io.github.ren.Dictation.svg',
         DATA / 'icons/hicolor/scalable/apps/io.github.ren.Dictation.svg')
    run('gdbus', 'call', '--session', '--dest', 'org.freedesktop.DBus',
        '--object-path', '/org/freedesktop/DBus', '--method', 'org.freedesktop.DBus.ReloadConfig')
    run('gtk-update-icon-cache', '-f', '-t', str(DATA / 'icons/hicolor'))
    run('update-desktop-database', str(DATA / 'applications'))
    if not run('gnome-extensions', 'enable', uuid):
        # GNOME discovers a newly installed extension at the next login.
        # The disabled list takes precedence, including after a reinstall.
        for key in ('enabled-extensions', 'disabled-extensions'):
            current = subprocess.check_output(['gsettings', 'get', 'org.gnome.shell', key],
                                              text=True, timeout=5).strip().removeprefix('@as ')
            entries = [entry for entry in ast.literal_eval(current)
                       if entry not in ('ren@local', 'lilt@local', 'ptt@local', uuid)]
            if key == 'enabled-extensions':
                entries.append(uuid)
            subprocess.run(['gsettings', 'set', 'org.gnome.shell', key, repr(entries)],
                           check=True, timeout=5)
    print('Installed Ren. Log out and back in to load the extension.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/ren')
    args = parser.parse_args()
    try:
        install(args.binary)
    except (OSError, ValueError, SyntaxError, subprocess.SubprocessError) as error:
        parser.exit(1, f'Installation failed: {error}\n')
