#!/usr/bin/env python3
"""Run installation checks, or only release payload checks with --archives DIR."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from xml.sax.saxutils import escape
import zipfile

ROOT = Path(__file__).resolve().parent.parent
BINARY = Path(os.environ.get('LILT_TEST_BINARY', ROOT / 'build/lilt'))
ARCHIVES = None
sys.path.insert(0, str(ROOT / 'scripts'))
from install_support import extension_files, extension_metadata  # noqa: E402


class InstallationFixture(unittest.TestCase):
    def setUp(self):
        if not shutil.which('glib-compile-schemas') or (not ARCHIVES and not BINARY.is_file()):
            self.skipTest('Built binary and glib-compile-schemas are required')
        self.directory = tempfile.TemporaryDirectory(prefix='lilt-package-test-')
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.stage = self.root / 'stage'
        self.prefix = Path('/opt/lilt test')
        self.data = Path('/var/lib/lilt test-data')
        self.config = Path('/var/lib/lilt test-config')
        self.uuid = extension_metadata()['uuid']
        self.environment = dict(os.environ)
        self.environment.update(HOME=str(self.root / 'home'), XDG_DATA_HOME=str(self.root / 'host-data'),
                                XDG_CONFIG_HOME=str(self.root / 'host-config'))
        self.environment.pop('LD_LIBRARY_PATH', None)
        # Fail visibly if a staging operation accidentally contacts the desktop.
        tools = self.root / 'trap-tools'
        tools.mkdir()
        for command in ('gnome-extensions', 'gsettings', 'gdbus', 'update-desktop-database'):
            script = tools / command
            script.write_text('#!/usr/bin/env python3\nfrom pathlib import Path\nPath(' +
                              repr(str(self.root / 'desktop-touched')) + ').touch()\nraise SystemExit(99)\n')
            script.chmod(0o755)
        self.environment['PATH'] = str(tools) + os.pathsep + self.environment['PATH']

    def stage_path(self, logical):
        return self.stage / logical.relative_to('/')

    def run_script(self, script, *arguments):
        result = subprocess.run([sys.executable, str(script), *map(str, arguments)],
                                env=self.environment, text=True, capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse((self.root / 'desktop-touched').exists(), result.stdout + result.stderr)
        return result

    def install(self, source=ROOT):
        arguments = ['--prefix', self.prefix, '--data-dir', self.data, '--destdir', self.stage]
        if source == ROOT:
            arguments += ['--binary', BINARY]
        self.run_script(source / 'scripts/install.py', *arguments)


class InstallationTests(InstallationFixture):
    def test_staged_install_upgrade_and_uninstall_preserve_data(self):
        self.install()
        installed = self.stage_path(self.prefix / 'bin/lilt')
        self.assertTrue(os.access(installed, os.X_OK))
        assets = self.stage_path(self.prefix / 'share/lilt')
        self.assertTrue((assets / 'download-model.py').is_file())
        self.assertTrue((assets / 'LICENSE').is_file())
        extension = self.stage_path(self.data / 'gnome-shell/extensions' / self.uuid)
        self.assertTrue((extension / 'schemas/gschemas.compiled').is_file())
        self.assertFalse((extension / 'tests').exists())
        desktop = self.stage_path(self.data / 'applications/io.github.lilt.Dictation.desktop').read_text()
        self.assertIn('Exec="/opt/lilt test/bin/lilt"', desktop)
        self.assertNotIn(str(self.stage), desktop)
        stale = extension / 'obsolete-module.js'
        stale.write_text('// previous release')
        self.install()
        self.assertFalse(stale.exists(), 'Upgrade must not leave obsolete runtime modules')
        model = self.stage_path(Path(self.environment['XDG_DATA_HOME']) / 'lilt/models/model.bin')
        model.parent.mkdir(parents=True)
        model.write_bytes(b'preserve user model')
        settings = self.stage_path(self.config / 'lilt/config.ini')
        settings.parent.mkdir(parents=True)
        settings.write_text('[lilt]\n')
        self.run_script(assets / 'uninstall.py', '--destdir', self.stage, '--config-dir', self.config)
        self.assertFalse(installed.exists())
        self.assertFalse(extension.exists())
        self.assertEqual(model.read_bytes(), b'preserve user model')
        self.assertTrue(settings.exists())
        self.install()
        self.run_script(assets / 'uninstall.py', '--destdir', self.stage,
                        '--config-dir', self.config, '--purge-data')
        self.assertFalse(model.exists())
        self.assertFalse(settings.exists())
        self.assertFalse(assets.exists())

    def test_no_enable_never_contacts_desktop(self):
        prefix = self.root / 'standalone-prefix'
        self.run_script(ROOT / 'scripts/install.py', '--binary', BINARY, '--prefix', prefix, '--no-enable')
        self.assertTrue((prefix / 'bin/lilt').is_file())
        self.run_script(prefix / 'share/lilt/uninstall.py', '--no-enable')
        self.assertFalse((prefix / 'bin/lilt').exists())

    def test_binary_runs_without_build_library_path(self):
        if not shutil.which('readelf'):
            self.skipTest('readelf is unavailable')
        dynamic = subprocess.check_output(['readelf', '-d', str(BINARY)], text=True)
        self.assertNotRegex(dynamic, r'\((?:RPATH|RUNPATH)\)')
        moved = self.root / 'relocated-lilt'
        shutil.copy2(BINARY, moved)
        result = subprocess.run([str(moved), '--version'], env=self.environment,
                                text=True, capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertRegex(result.stdout, r'^lilt \d+\.\d+\.\d+\n$')
        self.assertFalse((self.root / 'host-config').exists(), '--version must not initialize application state')

    def test_staging_rejects_traversal_and_parent_symlinks(self):
        outside = self.root / 'outside'
        outside.mkdir()
        self.stage.mkdir()
        (self.stage / 'opt').symlink_to(outside, target_is_directory=True)
        for prefix in ('/opt/lilt', '/../../tmp/lilt-escape'):
            result = subprocess.run([sys.executable, str(ROOT / 'scripts/install.py'),
                                     '--binary', str(BINARY), '--prefix', prefix,
                                     '--destdir', str(self.stage)], env=self.environment,
                                    text=True, capture_output=True, timeout=5)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(list(outside.iterdir()), [])

    def test_staging_rejects_symlinked_payload_paths(self):
        outside = self.root / 'outside-file'
        outside.write_text('preserve me')
        targets = (self.prefix / 'share/lilt/install.json',
                   self.data / 'applications/io.github.lilt.Dictation.desktop',
                   self.data / 'dbus-1/services/io.github.lilt.Dictation.service',
                   self.prefix / 'share/lilt/LICENSES')
        for logical in targets:
            with self.subTest(logical=str(logical)):
                shutil.rmtree(self.stage, ignore_errors=True)
                target = self.stage_path(logical)
                target.parent.mkdir(parents=True)
                target.symlink_to(outside)
                result = subprocess.run([sys.executable, str(ROOT / 'scripts/install.py'),
                                         '--binary', str(BINARY), '--prefix', str(self.prefix),
                                         '--data-dir', str(self.data), '--destdir', str(self.stage)],
                                        env=self.environment, text=True, capture_output=True, timeout=5)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(outside.read_text(), 'preserve me')

    def test_service_exec_quotes_special_characters(self):
        if not shutil.which('dbus-run-session') or not shutil.which('gdbus'):
            self.skipTest('Private D-Bus test tools unavailable')
        marker = self.root / 'service-started'
        fake = self.root / 'fake-lilt'
        fake.write_text('#!/usr/bin/env python3\nfrom pathlib import Path\nimport sys\nPath(' +
                        repr(str(marker)) + ').write_text(repr(sys.argv))\nraise SystemExit(1)\n')
        fake.chmod(0o755)
        prefix = self.root / "prefix % $ ' \\\" space"
        data = self.root / 'service-data'
        self.run_script(ROOT / 'scripts/install.py', '--binary', fake, '--prefix', prefix,
                        '--data-dir', data, '--no-enable')
        config = self.root / 'service.conf'
        config.write_text('<busconfig><type>session</type><listen>unix:tmpdir=/tmp</listen>'
                          '<servicedir>' + escape(str(data / 'dbus-1/services')) + '</servicedir>'
                          '<policy context="default"><allow send_destination="*"/>'
                          '<allow receive_sender="*"/><allow own="*"/></policy></busconfig>')
        result = subprocess.run(['dbus-run-session', '--config-file', str(config), '--',
                                 shutil.which('gdbus'), 'call', '--session', '--dest', 'org.freedesktop.DBus',
                                 '--object-path', '/org/freedesktop/DBus', '--method',
                                 'org.freedesktop.DBus.StartServiceByName', 'io.github.lilt.Dictation', '0'],
                                env=self.environment, text=True, capture_output=True, timeout=8)
        # The fake service exits deliberately; the marker proves its correctly
        # quoted executable path was reached through real private-bus activation.
        self.assertTrue(marker.is_file(), result.stdout + result.stderr)
        self.assertIn('--daemon', marker.read_text())


class ArchiveTests(InstallationFixture):
    def test_release_archives(self):
        if not ARCHIVES:
            self.skipTest('Pass --archives DIR to verify release archives')
        sums = (ARCHIVES / 'SHA256SUMS').read_text().splitlines()
        self.assertEqual(len(sums), 3)
        for entry in sums:
            expected, name = entry.split('  ', 1)
            with (ARCHIVES / name).open('rb') as payload:
                self.assertEqual(hashlib.file_digest(payload, 'sha256').hexdigest(), expected, name)
        extension_zip = next(ARCHIVES.glob('*-extension.zip'))
        with zipfile.ZipFile(extension_zip) as archive:
            expected = {str(path.relative_to(ROOT / 'extension')) for path in extension_files()}
            expected.update(('LICENSE', 'schemas/gschemas.compiled'))
            self.assertEqual(set(archive.namelist()), expected)
            self.assertEqual(json.loads(archive.read('metadata.json'))['uuid'], self.uuid)
            self.assertFalse(any(name.endswith(('.so', '.bin', '.py')) for name in archive.namelist()))
        bundle_archive = next(ARCHIVES.glob('*-ubuntu-24.04-x86_64.tar.gz'))
        extracted = self.root / 'extracted'
        with tarfile.open(bundle_archive) as archive:
            self.assertFalse(any(member.name.startswith('/') or '..' in Path(member.name).parts for member in archive))
            archive.extractall(extracted, filter='data')
        bundle = next(extracted.iterdir())
        self.install(bundle)
        self.assertTrue(self.stage_path(self.prefix / 'bin/lilt').is_file())
        with tarfile.open(next(ARCHIVES.glob('*-source.tar.gz'))) as archive:
            names = archive.getnames()
            source = names[0].split('/')[0]
            self.assertIn(source + '/vendor/whisper.cpp/CMakeLists.txt', names)
            self.assertIn(source + '/vendor/whisper.cpp/LICENSE', names)
            self.assertFalse(any(part in ('.git', '.deps', '__pycache__') for name in names for part in Path(name).parts))
            self.assertFalse(any(name.endswith('/gschemas.compiled') for name in names))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--archives', type=Path)
    options, remaining = parser.parse_known_args()
    ARCHIVES = options.archives.resolve() if options.archives else None
    unittest.main(defaultTest='ArchiveTests' if ARCHIVES else 'InstallationTests',
                  argv=[sys.argv[0], *remaining])
