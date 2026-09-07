#!/usr/bin/env python3
"""Remove user-installed lilt files; keep settings and models unless requested."""

import argparse
import os
from pathlib import Path
import shutil
import sys

from install_support import (ROOT, absolute_path, add_layout_arguments, best_effort,
                             extension_metadata, layout_from_args, refresh_desktop)


def remove(path):
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.is_dir():
        shutil.rmtree(path)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    add_layout_arguments(parser)
    parser.add_argument('--purge-data', action='store_true', help='also delete lilt settings and downloaded models')
    parser.add_argument('--config-dir', type=absolute_path,
                        default=absolute_path(os.environ.get('XDG_CONFIG_HOME') or Path.home() / '.config'),
                        help='configuration base directory (used only with --purge-data)')
    parser.add_argument('--user-data-dir', type=absolute_path,
                        default=absolute_path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share'),
                        help='runtime user data base directory (used only with --purge-data)')
    args = parser.parse_args(argv)
    try:
        layout = layout_from_args(args)
        uuid = args.installation_defaults.get('uuid')
        if not uuid and (ROOT / 'extension/metadata.json').is_file():
            uuid = extension_metadata()['uuid']
        if not uuid or '/' in uuid or uuid in ('.', '..'):
            raise ValueError('Cannot determine the installed extension UUID')
        if not args.destdir and not args.no_enable:
            best_effort(['gnome-extensions', 'disable', uuid])
            if layout.binary.is_file():
                best_effort([str(layout.binary), '--quit'])
        for path in (layout.binary, layout.data / 'gnome-shell/extensions' / uuid,
                     layout.data / 'applications/io.github.lilt.Dictation.desktop',
                     layout.data / 'dbus-1/services/io.github.lilt.Dictation.service',
                     layout.data / 'icons/hicolor/scalable/apps/io.github.lilt.Dictation.svg'):
            remove(layout.staged(path))
        if args.purge_data:
            remove(layout.staged(layout.assets))
            if args.user_data_dir / 'lilt' != layout.assets:
                remove(layout.staged(args.user_data_dir / 'lilt'))
            remove(layout.staged(args.config_dir / 'lilt'))
        else:
            for name in ('download-model.py', 'uninstall.py', 'install_support.py', 'install.json',
                         'LICENSE', 'LICENSES', 'THIRD_PARTY_NOTICES.md', '__pycache__'):
                remove(layout.staged(layout.assets / name))
            assets = layout.staged(layout.assets)
            if assets.is_dir() and not any(assets.iterdir()):
                assets.rmdir()
        if not args.destdir and not args.no_enable:
            refresh_desktop(layout)
        print('Removed lilt.' + (' Settings and models deleted.' if args.purge_data else ' Settings and models preserved.'))
    except (OSError, ValueError, argparse.ArgumentTypeError) as error:
        parser.exit(1, 'Removal failed: ' + str(error) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
