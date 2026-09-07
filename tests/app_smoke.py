#!/usr/bin/env python3
"""Run inside dbus-run-session and Xvfb. Never uses a real microphone."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import gi

gi.require_version('Gtk', '3.0')
gi.require_version('Gdk', '3.0')
from gi.repository import Gio, GLib, Gdk

root = Path(__file__).resolve().parent.parent
(root / '.deps').mkdir(exist_ok=True)
bus = Gio.bus_get_sync(Gio.BusType.SESSION)
name = 'io.github.lilt.Dictation'
path = '/io/github/lilt/Dictation'

def call(method, args=None):
    return bus.call_sync(name, path, name, method, args, None, Gio.DBusCallFlags.NONE, 2000, None)

def state():
    return bus.call_sync(name, path, 'org.freedesktop.DBus.Properties', 'Get',
        GLib.Variant('(ss)', (name, 'State')), GLib.VariantType('(v)'), Gio.DBusCallFlags.NONE, 2000, None).unpack()[0]

def until(predicate, timeout=3):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        try:
            if predicate(): return
        except GLib.Error:
            pass
        while GLib.MainContext.default().iteration(False): pass
        time.sleep(.02)
    raise AssertionError('Timed out waiting for application state')

with tempfile.TemporaryDirectory(prefix='lilt-app-test-') as temp:
    # Reach the audio-open path without reading the user's models. Inference
    # cannot run because the private PulseAudio address is deliberately absent.
    model = Path(temp, 'data/lilt/models/ggml-small.en-q5_1.bin')
    model.parent.mkdir(parents=True)
    model.touch()
    env = dict(os.environ, XDG_CONFIG_HOME=temp, XDG_DATA_HOME=temp + '/data', PULSE_SERVER='unix:' + temp + '/missing-audio', NO_AT_BRIDGE='1', GTK_USE_PORTAL='0', GDK_BACKEND='x11')
    log = open(root / '.deps/app-smoke.log', 'w+')
    process = subprocess.Popen([str(root / 'build/lilt'), '--daemon'], env=env, stdout=log, stderr=log)
    try:
        until(lambda: state() == 'idle')
        finish = bus.call_sync(name, path, 'org.freedesktop.DBus.Properties', 'Get',
            GLib.Variant('(ss)', (name, 'FinishShortcut')), GLib.VariantType('(v)'),
            Gio.DBusCallFlags.NONE, 2000, None).unpack()[0]
        assert finish == 'Return', 'Existing settings must default Finish to Enter'
        preview = bus.call_sync(name, path, 'org.freedesktop.DBus.Properties', 'Get',
            GLib.Variant('(ss)', (name, 'LivePreview')), GLib.VariantType('(v)'),
            Gio.DBusCallFlags.NONE, 2000, None).unpack()[0]
        assert preview is True, 'Existing settings must default Live text to enabled'
        # Fails closed if no desktop integration is attached.
        call('Toggle')
        assert state() == 'error'
        # Only the Shell owner can register; a regular process cannot attach.
        try:
            call('Attach')
            raise AssertionError('Untrusted Attach succeeded')
        except GLib.Error:
            pass
        bus.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus', 'RequestName',
            GLib.Variant('(su)', ('org.gnome.Shell', 0)), None, Gio.DBusCallFlags.NONE, 1000, None)
        call('Attach')
        call('Cancel')
        assert state() == 'idle'
        # Deliberately missing audio server: start and cancellation are bounded.
        for _ in range(4):
            call('Toggle')
            call('Cancel')
            assert state() == 'idle'
            time.sleep(.05)
        # An insertion failure remains visible after late worker callbacks.
        call('Toggle')
        call('ReportError', GLib.Variant('(s)', ('Test insertion failure',)))
        time.sleep(.2)
        assert state() == 'error'
        call('Cancel')
        call('ShowPreferences')
        time.sleep(.3)
        # A second launch reaches the existing instance and exits promptly.
        subprocess.run([str(root / 'build/lilt')], env=env, timeout=3, check=True, stdout=log, stderr=log)
        call('ShowPreferences')
        time.sleep(.3)
        display = Gdk.Display.open(os.environ['DISPLAY'])
        screen = display.get_default_screen()
        window = screen.get_root_window()
        pixbuf = Gdk.pixbuf_get_from_window(window, 0, 0, window.get_width(), window.get_height())
        pixbuf.savev(str(root / '.deps/preferences.png'), 'png', [], [])
        rss = next(line.strip() for line in Path(f'/proc/{process.pid}/status').read_text().splitlines() if line.startswith('VmRSS:'))
        print('Native app smoke passed: service, trusted Attach, cancel/restart, persistent error, single instance, UI render. ' + rss)
        call('Detach')
        call('Quit')
        process.wait(timeout=4)
        assert process.returncode == 0
        log.flush()
        log.seek(0)
        content = log.read()
        if 'CRITICAL' in content or 'WARNING' in content:
            raise AssertionError(content)
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=4)
        log.close()
