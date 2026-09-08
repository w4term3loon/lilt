#!/usr/bin/env python3
"""Install lilt for the current user, without sudo."""

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
    uuid = json.loads((ROOT / 'extension/metadata.json').read_text())['uuid']
    extensions = DATA / 'gnome-shell/extensions'
    extensions.mkdir(parents=True, exist_ok=True)
    target = extensions / uuid
    with tempfile.TemporaryDirectory(prefix='.lilt-', dir=extensions) as temporary:
        prepared = Path(temporary) / uuid
        prepared.mkdir()
        files = [*ROOT.glob('extension/*.js'), *ROOT.glob('extension/*.svg'), ROOT / 'extension/metadata.json',
                 ROOT / 'extension/stylesheet.css', *ROOT.glob('extension/schemas/*.xml')]
        for source in files:
            copy(source, prepared / source.relative_to(ROOT / 'extension'))
        copy(ROOT / 'LICENSE', prepared / 'LICENSE')
        subprocess.run(['glib-compile-schemas', '--strict', str(prepared / 'schemas')], check=True)
        copy(binary, PREFIX / 'bin/lilt')
        for name in ('download-model.py', 'uninstall.py'):
            copy(ROOT / 'scripts' / name, PREFIX / 'share/lilt' / name)
        for name in ('LICENSE', 'THIRD_PARTY_NOTICES.md', 'LICENSES/whisper.cpp-MIT.txt'):
            copy(ROOT / name, PREFIX / 'share/lilt' / name)
        previous = Path(temporary) / 'previous'
        if target.exists():
            target.rename(previous)
        try:
            prepared.rename(target)
        except OSError:
            if previous.exists():
                previous.rename(target)
            raise

    # A stale PTT service starts lilt under the wrong bus name and times out.
    run('gnome-extensions', 'disable', 'ptt@local')
    legacy = extensions / 'ptt@local'
    if legacy.is_symlink():
        legacy.unlink()
    elif legacy.exists():
        shutil.rmtree(legacy)
    for directory, suffix in (('dbus-1/services', 'service'), ('applications', 'desktop')):
        (DATA / directory / f'io.github.ptt.Dictation.{suffix}').unlink(missing_ok=True)
    alias = PREFIX / 'bin/ptt'
    if alias.is_symlink() and alias.resolve() == PREFIX / 'bin/lilt':
        alias.unlink()
    for obsolete in ('install_support.py', 'install.json'):
        (PREFIX / 'share/lilt' / obsolete).unlink(missing_ok=True)

    binary = PREFIX / 'bin/lilt'
    # Desktop Exec and D-Bus Exec use different quoting rules.
    quoted = str(binary).replace('\\', '\\\\').replace('"', '\\"').replace('`', '\\`').replace('$', '\\$').replace('%', '%%')
    quoted = '"' + quoted.replace('\\', '\\\\') + '"'
    service_command = shlex.quote(str(binary)).replace('\\', '\\\\')
    registrations = {
        'applications/io.github.lilt.Dictation.desktop':
            '[Desktop Entry]\nType=Application\nName=lilt\n'
            f'Exec={quoted}\nIcon=io.github.lilt.Dictation\nTerminal=false\nCategories=Utility;Accessibility;\n',
        'dbus-1/services/io.github.lilt.Dictation.service':
            '[D-BUS Service]\nName=io.github.lilt.Dictation\n'
            f'Exec={service_command} --daemon\n',
    }
    for name, content in registrations.items():
        path = DATA / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
    copy(ROOT / 'data/io.github.lilt.Dictation.svg',
         DATA / 'icons/hicolor/scalable/apps/io.github.lilt.Dictation.svg')
    run('gdbus', 'call', '--session', '--dest', 'org.freedesktop.DBus',
        '--object-path', '/org/freedesktop/DBus', '--method', 'org.freedesktop.DBus.ReloadConfig')
    run('gtk-update-icon-cache', '-f', '-t', str(DATA / 'icons/hicolor'))
    run('update-desktop-database', str(DATA / 'applications'))
    if not run('gnome-extensions', 'enable', uuid):
        # GNOME discovers a newly installed extension at the next login.
        current = subprocess.check_output(['gsettings', 'get', 'org.gnome.shell', 'enabled-extensions'],
                                          text=True, timeout=5).strip().removeprefix('@as ')
        enabled = [entry for entry in ast.literal_eval(current) if entry not in ('ptt@local', uuid)]
        subprocess.run(['gsettings', 'set', 'org.gnome.shell', 'enabled-extensions', repr([*enabled, uuid])],
                       check=True, timeout=5)
        print('Log out and back in to load the extension.')
    print('Installed lilt. Log out and back in after an upgrade.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/lilt')
    args = parser.parse_args()
    try:
        install(args.binary)
    except (OSError, ValueError, SyntaxError, subprocess.SubprocessError) as error:
        parser.exit(1, f'Installation failed: {error}\n')
