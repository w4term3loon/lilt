#!/usr/bin/env python3
"""Shared file layout for source, release, and staged lilt installations."""

import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def absolute_path(value):
    path = Path(value).expanduser()
    if not path.is_absolute() or '..' in path.parts or any(char in str(path) for char in '\n\r\0'):
        raise argparse.ArgumentTypeError('Use an absolute path without "..": ' + str(value))
    return path


def installation_defaults():
    manifest = Path(__file__).with_name('install.json')
    return json.loads(manifest.read_text()) if manifest.is_file() else {}


def add_layout_arguments(parser):
    defaults = installation_defaults()
    parser.add_argument('--prefix', type=absolute_path,
                        help='executable prefix (default: ~/.local)')
    parser.add_argument('--data-dir', type=absolute_path,
                        help='data directory (default: XDG_DATA_HOME or PREFIX/share)')
    parser.add_argument('--destdir', type=absolute_path,
                        help='stage files under this root; never changes the desktop session')
    parser.add_argument('--no-enable', action='store_true',
                        help='do not contact the desktop session or change extension state')
    parser.set_defaults(installation_defaults=defaults)


@dataclass(frozen=True)
class Layout:
    prefix: Path
    data: Path
    destdir: Path | None = None

    def staged(self, path):
        if not self.destdir:
            return path
        base = self.destdir.resolve()
        target = base / path.relative_to('/')
        if not target.resolve().is_relative_to(base):
            raise ValueError('Staged path escapes --destdir: ' + str(target))
        return target

    @property
    def binary(self):
        return self.prefix / 'bin/lilt'

    @property
    def assets(self):
        # Executable-relative scripts remain discoverable with custom XDG data.
        return self.prefix / 'share/lilt'


def layout_from_args(args):
    defaults = args.installation_defaults
    prefix = args.prefix or absolute_path(defaults.get('prefix', Path.home() / '.local'))
    if args.data_dir:
        data = args.data_dir
    elif args.prefix:
        data = prefix / 'share'
    else:
        data = absolute_path(defaults.get('data') or os.environ.get('XDG_DATA_HOME') or prefix / 'share')
    return Layout(prefix, data, args.destdir)


def extension_metadata():
    metadata = json.loads((ROOT / 'extension/metadata.json').read_text(encoding='utf-8'))
    uuid = metadata.get('uuid', '')
    if not isinstance(uuid, str) or not uuid or '/' in uuid or uuid in ('.', '..'):
        raise ValueError('Invalid extension UUID in metadata.json')
    return metadata


def extension_files():
    """Only runtime code/assets enter an installation or extension ZIP."""
    extension = ROOT / 'extension'
    files = [extension / 'metadata.json', extension / 'stylesheet.css']
    files += sorted(extension.glob('*.js'))
    files += sorted((extension / 'schemas').glob('*.gschema.xml'))
    for path in files:
        if not path.is_file() or path.is_symlink():
            raise ValueError('Missing or unsafe extension runtime file: ' + str(path))
    if not (extension / 'extension.js') in files or not any(path.name.endswith('.gschema.xml') for path in files):
        raise ValueError('Extension entry point and settings schema are required')
    return files


def copy_file(source, target, mode=None):
    target.parent.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix='.' + target.name + '.', dir=target.parent)
    os.close(descriptor)
    temporary = Path(name)
    try:
        shutil.copy2(source, temporary)
        if mode is not None:
            temporary.chmod(mode)
        temporary.replace(target)
    finally:
        temporary.unlink(missing_ok=True)


def write_text(target, content):
    target.parent.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix='.' + target.name + '.', dir=target.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, 'w', encoding='utf-8') as output:
            output.write(content)
        temporary.chmod(0o644)
        temporary.replace(target)
    finally:
        temporary.unlink(missing_ok=True)


def copy_extension(target):
    target.mkdir(parents=True, exist_ok=True)
    for source in extension_files():
        copy_file(source, target / source.relative_to(ROOT / 'extension'))
    copy_file(ROOT / 'LICENSE', target / 'LICENSE')
    subprocess.run(['glib-compile-schemas', '--strict', str(target / 'schemas')], check=True)


def desktop_quote(value):
    # Exec has its own quoting rules; desktop files decode backslashes first.
    escaped = value.replace('\\', '\\\\').replace('"', '\\"').replace('`', '\\`').replace('$', '\\$').replace('%', '%%')
    return '"' + escaped.replace('\\', '\\\\') + '"'


def best_effort(command):
    if shutil.which(command[0]):
        try:
            return subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                  timeout=5, check=False).returncode == 0
        except subprocess.TimeoutExpired:
            pass
    return False


def refresh_desktop(layout):
    best_effort(['gdbus', 'call', '--session', '--dest', 'org.freedesktop.DBus',
                 '--object-path', '/org/freedesktop/DBus', '--method', 'org.freedesktop.DBus.ReloadConfig'])
    best_effort(['update-desktop-database', str(layout.data / 'applications')])
