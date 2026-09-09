#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check an upgrade and removal without touching the current desktop or data."""

import ast
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent


class InstallationTest(unittest.TestCase):
    def test_upgrade_and_removal(self):
        uuid = json.loads((ROOT / 'extension/metadata.json').read_text())['uuid']
        settings = {'favorite-apps': ['other.desktop', 'io.github.lilt.Dictation.desktop'],
                    'enabled-extensions': ['other@example.com', 'ren@local'],
                    'disabled-extensions': [uuid]}
        commands = []
        real_run = subprocess.run

        def desktop(command, **kwargs):
            command = list(command)
            commands.append(command)
            output = ''
            if command[0] == 'glib-compile-schemas':
                return real_run(command, **kwargs)
            if command[:2] == ['gsettings', 'get']:
                output = repr(settings[command[3]]) + '\n'
            elif command[:2] == ['gsettings', 'set']:
                settings[command[3]] = ast.literal_eval(command[4])
            elif command[:2] == ['gnome-extensions', 'disable']:
                settings['enabled-extensions'] = [entry for entry in settings['enabled-extensions']
                                                   if entry != command[2]]
                settings['disabled-extensions'].append(command[2])
            # A freshly installed extension is not discoverable until next login.
            status = int(command[:2] == ['gnome-extensions', 'enable'])
            return subprocess.CompletedProcess(command, status, output, '')

        with tempfile.TemporaryDirectory(prefix='ren-install-test-') as temporary:
            home = Path(temporary) / "home with 'quotes'"
            prefix, data, config = home / '.local', home / 'data', home / 'config'
            extensions = data / 'gnome-shell/extensions'
            retained = [data / 'ren/models/custom.bin', config / 'ren/config.ini',
                        data / 'lilt/models/ggml-old.bin']
            for path in retained:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('user data')
            for old in ('ren@local', uuid):
                (extensions / old).mkdir(parents=True)
                (extensions / old / 'stale.js').touch()
            (extensions / 'lilt@local').symlink_to(data / 'lilt', target_is_directory=True)
            binary = Path(temporary) / 'ren'
            binary.write_bytes(b'test executable')
            binary.chmod(0o755)

            with patch.dict(os.environ, HOME=str(home), XDG_DATA_HOME=str(data),
                            XDG_CONFIG_HOME=str(config)), patch('subprocess.run', side_effect=desktop):
                install = runpy.run_path(str(ROOT / 'scripts/install.py'))['install']
                install(binary)
                self.assertEqual((prefix / 'bin/ren').read_bytes(), binary.read_bytes())
                self.assertTrue((extensions / uuid / 'schemas/gschemas.compiled').is_file())
                self.assertFalse((extensions / uuid / 'stale.js').exists())
                self.assertFalse((extensions / 'ren@local').exists())
                self.assertFalse((extensions / 'lilt@local').is_symlink())
                self.assertEqual(settings['enabled-extensions'], ['other@example.com', uuid])
                self.assertEqual(settings['disabled-extensions'], [])
                self.assertEqual(settings['favorite-apps'], ['other.desktop', 'io.github.ren.Dictation.desktop'])

                def uninstall(*options):
                    script = prefix / 'share/ren/uninstall.py'
                    with patch.object(sys, 'argv', [str(script), *options]):
                        runpy.run_path(str(script), run_name='__main__')
                    self.assertFalse((prefix / 'bin/ren').exists())
                    self.assertEqual((extensions / uuid).exists(), '--app-only' in options)
                    self.assertFalse((data / 'applications/io.github.ren.Dictation.desktop').exists())
                    self.assertFalse((data / 'dbus-1/services/io.github.ren.Dictation.service').exists())

                # Native updates/removal must preserve the store's files, version and enable state.
                store_metadata = extensions / uuid / 'metadata.json'
                store_metadata.write_text(json.dumps({'uuid': uuid, 'version': 1}))
                store_files = {str(path.relative_to(extensions)): path.read_bytes()
                               for path in extensions.rglob('*') if path.is_file()}
                for update in (False, True):
                    if update:
                        settings['enabled-extensions'].remove(uuid)
                        settings['disabled-extensions'].append(uuid)
                    original_settings = {key: value[:] for key, value in settings.items()}
                    commands.clear()
                    if update:
                        uninstall('--app-only')
                    install(binary, app_only=True)
                    self.assertEqual(settings, original_settings)
                    self.assertEqual(store_files, {str(path.relative_to(extensions)): path.read_bytes()
                                                   for path in extensions.rglob('*') if path.is_file()})
                    self.assertFalse(any(command[0] == 'gnome-extensions' for command in commands))
                    self.assertEqual((prefix / 'bin/ren').read_bytes(), binary.read_bytes())
                    self.assertTrue((data / 'dbus-1/services/io.github.ren.Dictation.service').is_file())

                uninstall()
                self.assertTrue(all(path.read_text() == 'user data' for path in retained))
                install(binary)
                self.assertNotIn(uuid, settings['disabled-extensions'])
                uninstall('--purge-data')
                self.assertFalse((data / 'ren').exists())
                self.assertFalse((config / 'ren').exists())
                self.assertEqual(retained[-1].read_text(), 'user data')


if __name__ == '__main__':
    unittest.main()
