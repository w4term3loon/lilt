#!/usr/bin/env python3
"""Remove ren; keep settings and models unless --purge-data is given."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess


def remove(path):
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.is_dir():
        shutil.rmtree(path)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--purge-data', action='store_true')
    args = parser.parse_args()
    prefix = Path.home() / '.local'
    data = Path(os.environ.get('XDG_DATA_HOME') or prefix / 'share')
    config = Path(os.environ.get('XDG_CONFIG_HOME') or Path.home() / '.config')
    try:
        if not data.is_absolute() or not config.is_absolute():
            raise ValueError('XDG directories must be absolute paths.')
        uuid = 'ren@w4term3loon.github.io'
        for command in (['gnome-extensions', 'disable', uuid], [str(prefix / 'bin/ren'), '--quit']):
            try:
                subprocess.run(command, capture_output=True, timeout=5)
            except (OSError, subprocess.TimeoutExpired):
                pass
        for path in (prefix / 'bin/ren', data / 'gnome-shell/extensions' / uuid,
                     data / 'applications/io.github.ren.Dictation.desktop',
                     data / 'dbus-1/services/io.github.ren.Dictation.service',
                     data / 'icons/hicolor/scalable/apps/io.github.ren.Dictation.svg'):
            remove(path)
        if args.purge_data:
            remove(prefix / 'share/ren')
            remove(data / 'ren')
            remove(config / 'ren')
        else:
            for name in ('download-model.py', 'uninstall.py', 'LICENSE', 'LICENSES', 'THIRD_PARTY_NOTICES.md', '__pycache__'):
                remove(prefix / 'share/ren' / name)
        print('Removed Ren.' + (' Settings and models deleted.' if args.purge_data else ' Settings and models kept.'))
    except (OSError, ValueError) as error:
        parser.exit(1, f'Removal failed: {error}\n')
