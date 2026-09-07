#!/usr/bin/env python3
"""Exercise actual service activation and extension Gio calls on private X/D-Bus.

Run: python3 tests/native_activation.py [--native-only] [--binary /path/to/lilt]
No GNOME Shell, microphone, model, or existing user configuration is accessed.
"""
import argparse
import configparser
import os
from pathlib import Path
import select
import shlex
import shutil
import subprocess
import tempfile
from xml.sax.saxutils import escape


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path,
                        default=Path(os.environ.get('LILT_TEST_BINARY', root / 'build/lilt')))
    parser.add_argument('--extension', type=Path, default=root / 'extension/extension.js')
    parser.add_argument('--native-only', action='store_true')
    args = parser.parse_args()
    binary = args.binary.resolve()
    if not binary.is_file():
        parser.error(f'Build lilt first: {binary} is missing')
    xvfb = shutil.which('Xvfb') or str(root / '.deps/sysroot/usr/bin/Xvfb')
    if not Path(xvfb).is_file():
        parser.error('Xvfb is required (install the Ubuntu xvfb package)')

    with tempfile.TemporaryDirectory(prefix='lilt-activation-test-') as directory:
        temp = Path(directory)
        for child in ('home', 'data', 'config/ptt', 'cache', 'runtime', 'services'):
            (temp / child).mkdir(parents=True, exist_ok=True)
        (temp / 'runtime').chmod(0o700)
        legacy_settings = (
            '[PTT]\nshortcut=<Control><Alt>space\nfinish_shortcut=<Shift>F8\nmodel=medium-q5_0\nlanguage=hu\nlive_preview=false\n')
        (temp / 'config/ptt/config.ini').write_text(legacy_settings)
        launcher = temp / 'start-lilt'
        launcher.write_text('#!/bin/sh\nexec ' + shlex.quote(str(binary)) + ' --daemon\n')
        launcher.chmod(0o700)
        (temp / 'services/io.github.lilt.Dictation.service').write_text(
            '[D-BUS Service]\nName=io.github.lilt.Dictation\nExec=' + str(launcher) + '\n')
        config = temp / 'session.conf'
        config.write_text('''<busconfig>
  <type>session</type>
  <listen>unix:tmpdir=/tmp</listen>
  <servicedir>''' + escape(str(temp / 'services')) + '''</servicedir>
  <policy context="default">
    <allow send_destination="*"/>
    <allow receive_sender="*"/>
    <allow own="*"/>
  </policy>
</busconfig>
''')
        env = dict(os.environ)
        for key in ('DBUS_SESSION_BUS_ADDRESS', 'DBUS_STARTER_ADDRESS',
                    'DBUS_STARTER_BUS_TYPE', 'WAYLAND_DISPLAY'):
            env.pop(key, None)
        env.update(
            HOME=str(temp / 'home'), XDG_DATA_HOME=str(temp / 'data'),
            XDG_CONFIG_HOME=str(temp / 'config'), XDG_CACHE_HOME=str(temp / 'cache'),
            XDG_RUNTIME_DIR=str(temp / 'runtime'), XDG_DATA_DIRS='/usr/share',
            GSETTINGS_BACKEND='memory', PULSE_SERVER='unix:' + str(temp / 'missing-audio'),
            NO_AT_BRIDGE='1', GTK_USE_PORTAL='0', GDK_BACKEND='x11',
            LILT_TEST_ISOLATED='1', LILT_TEST_EXTENSION_SOURCE=str(args.extension.resolve()),
        )
        read_fd, write_fd = os.pipe()
        with (temp / 'xvfb.log').open('w+') as xlog:
            display = subprocess.Popen(
                [xvfb, '-displayfd', str(write_fd), '-screen', '0', '800x640x24',
                 '-nolisten', 'tcp', '-ac'],
                env=env, pass_fds=(write_fd,), stdout=xlog, stderr=xlog,
            )
            os.close(write_fd)
            try:
                if not select.select([read_fd], [], [], 5)[0]:
                    raise RuntimeError('Xvfb did not report a display within 5 seconds')
                number = os.read(read_fd, 64).decode().strip()
                if not number.isdigit():
                    xlog.seek(0)
                    raise RuntimeError('Xvfb failed: ' + xlog.read())
                env['DISPLAY'] = ':' + number
                command = ['dbus-run-session', '--config-file=' + str(config), '--',
                           'gjs', str(root / 'tests/proxy_startup.js')]
                if args.native_only:
                    command.append('--native-only')
                result = subprocess.run(command, env=env, capture_output=True,
                                        text=True, timeout=50)
                print(result.stdout, end='')
                if result.returncode or any(word in result.stderr for word in ('CRITICAL', 'WARNING')):
                    raise RuntimeError('Activation regression failed:\n' + result.stderr)
                saved = configparser.ConfigParser(interpolation=None)
                saved.read(temp / 'config/lilt/config.ini')
                assert saved['lilt']['model'] == 'medium.en-q5_0', 'Preserve model size when migrating to English'
                assert saved['lilt']['language'] == 'en', 'Ignore and replace legacy language settings'
                assert saved['lilt']['shortcut'] == '<Control><Alt>space'
                assert saved['lilt']['finish_shortcut'] == '<Shift>F8'
                assert saved['lilt'].getboolean('live_preview') is False
                assert (temp / 'config/ptt/config.ini').read_text() == legacy_settings, 'Legacy configuration remains untouched'
                print('Name and English migration passed: model size, shortcuts, and live text preference preserved')
                print('Isolated native activation test passed with no GTK/GLib warnings')
            finally:
                os.close(read_fd)
                display.terminate()
                display.wait(timeout=5)


if __name__ == '__main__':
    main()
