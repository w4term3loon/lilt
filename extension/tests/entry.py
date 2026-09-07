"""Fake recognizer and real GTK text fields for the isolated Shell integration test."""
import gi
import json
import math
import os
from pathlib import Path

gi.require_version('Gtk', '3.0')
gi.require_version('IBus', '1.0')
from gi.repository import Gtk, Gio, GLib, IBus

state = 'idle'
level = 0.5
tick = 0
finish_shortcut = '<Control>F8'
preedit = ''
counts = {'Stop': 0, 'Cancel': 0, 'Attach': 0, 'Detach': 0, 'Activate': 0,
          'text': '', 'preedit_events': [], 'snapshots': {}, 'selection_events': []}
output = Path(os.environ['LILT_SMOKE_ROOT']) / 'entry-result.json'
final_text = 'Clear words 🦉 Hello.'

def save():
    output.write_text(json.dumps(counts, ensure_ascii=False, indent=2))

xml = '''<node><interface name="io.github.lilt.Dictation"><method name="Attach"/><method name="Detach"/><method name="Toggle"/><method name="Stop"/><method name="Cancel"/><method name="ShowPreferences"/><method name="Quit"/><method name="ReportError"><arg type="s" direction="in"/></method><property name="State" type="s" access="read"/><property name="Level" type="d" access="read"/><property name="Message" type="s" access="read"/><property name="Shortcut" type="s" access="read"/><property name="FinishShortcut" type="s" access="read"/><property name="LivePreview" type="b" access="read"/><signal name="Transcript"><arg type="s"/></signal><signal name="PartialTranscript"><arg type="s"/><arg type="u"/></signal></interface><interface name="io.github.lilt.TestEntry"><method name="Prepare"><arg type="s" direction="in"/><arg type="i" direction="in"/><arg type="i" direction="in"/></method><method name="Snapshot"><arg type="s" direction="in"/></method><method name="FinishShortcut"><arg type="s" direction="in"/></method><method name="SwitchEngine"><arg type="s" direction="in"/></method><method name="Close"/></interface></node>'''

def props(conn, sender, path, interface, prop):
    return {'State': GLib.Variant('s', state), 'Level': GLib.Variant('d', level),
            'Message': GLib.Variant('s', ''),
            'Shortcut': GLib.Variant('s', '<Control><Super>space'),
            'FinishShortcut': GLib.Variant('s', finish_shortcut),
            'LivePreview': GLib.Variant('b', True)}[prop]

def changed_props(properties):
    bus.emit_signal(None, '/io/github/lilt/Dictation', 'org.freedesktop.DBus.Properties',
                    'PropertiesChanged', GLib.Variant('(sa{sv}as)',
                    ('io.github.lilt.Dictation', properties, [])))

def setstate(new):
    global state
    state = new
    changed_props({'State': GLib.Variant('s', state)})

def complete():
    bus.emit_signal(None, '/io/github/lilt/Dictation', 'io.github.lilt.Dictation',
                    'Transcript', GLib.Variant('(s)', ('Clear words 🦉\nHello.\r\n',)))
    setstate('idle')
    return False

def update_level():
    global level, tick
    if state == 'recording':
        tick += 1
        level = 0.5 + 0.45 * math.sin(tick * 0.8)
        changed_props({'Level': GLib.Variant('d', level)})
        stable = 'Clear <words> 🦉 '
        tentative = '& draft' if tick % 10 < 5 else '& revised'
        bus.emit_signal(None, '/io/github/lilt/Dictation', 'io.github.lilt.Dictation',
                        'PartialTranscript', GLib.Variant('(su)',
                        (stable + tentative, len(stable.encode('utf-8')))))
    return True

GLib.timeout_add(100, update_level)

def method(conn, sender, path, iface, name, args, call):
    global finish_shortcut
    values = args.unpack()
    if iface == 'io.github.lilt.TestEntry':
        if name == 'Prepare':
            entry.set_text(values[0])
            entry.select_region(values[1], values[2])
        elif name == 'Snapshot':
            current = ibus.get_global_engine()
            counts['snapshots'][values[0]] = {
                'text': entry.get_text(), 'preedit': preedit,
                'selection': entry.get_selection_bounds(),
                'other_text': other_entry.get_text(),
                'engine': current.get_name() if current else None,
                'Activate': counts['Activate'], 'Stop': counts['Stop'],
                'Cancel': counts['Cancel'],
            }
        elif name == 'FinishShortcut':
            finish_shortcut = values[0]
            changed_props({'FinishShortcut': GLib.Variant('s', finish_shortcut)})
        elif name == 'SwitchEngine':
            def switched(service, response):
                counts['engine_switch_ok'] = service.set_global_engine_async_finish(response)
                save()
            ibus.set_global_engine_async(values[0], 3000, None, switched)
        elif name == 'Close':
            win.destroy()
        save()
        call.return_value(None)
        return
    counts[name] = counts.get(name, 0) + 1
    if name == 'Toggle':
        setstate('recording')
    elif name == 'Stop':
        setstate('transcribing')
        GLib.timeout_add(400, complete)
    elif name == 'Cancel':
        # An already queued result must not resurrect a canceled composition.
        bus.emit_signal(None, '/io/github/lilt/Dictation', 'io.github.lilt.Dictation',
                        'Transcript', GLib.Variant('(s)', ('cancelled late text',)))
        bus.emit_signal(None, '/io/github/lilt/Dictation', 'io.github.lilt.Dictation',
                        'PartialTranscript', GLib.Variant('(su)', ('cancelled late draft', 0)))
        setstate('idle')
    elif name == 'ReportError':
        counts.setdefault('errors', []).append(values[0])
    save()
    call.return_value(None)

def ready(conn, name):
    global bus
    bus = conn
    for interface in Gio.DBusNodeInfo.new_for_xml(xml).interfaces:
        conn.register_object('/io/github/lilt/Dictation', interface, method, props, None)

Gio.bus_own_name(Gio.BusType.SESSION, 'io.github.lilt.Dictation',
                 Gio.BusNameOwnerFlags.NONE, ready, None, None)
IBus.init()
ibus = IBus.Bus()
win = Gtk.Window(title='lilt test entry')
entry = Gtk.Entry()
win.add(entry)
win.set_default_size(850, 90)
other = Gtk.Window(title='lilt other entry')
other_entry = Gtk.Entry()
other.add(other_entry)
other.set_default_size(500, 80)

def changed(widget):
    counts['text'] = widget.get_text()
    save()

def preedited(widget, text):
    global preedit
    preedit = text
    counts['preedit_events'].append(text)
    save()

def activated(widget):
    counts['Activate'] += 1
    save()

def selection_changed(widget, parameter):
    counts['selection_events'].append({'time': GLib.get_monotonic_time(), 'property': parameter.name, 'selection': widget.get_selection_bounds(), 'cursor': widget.get_position(), 'text': widget.get_text()})
    save()

entry.connect('notify::selection-bound', selection_changed)
entry.connect('notify::cursor-position', selection_changed)
entry.connect('changed', changed)
entry.connect('preedit-changed', preedited)
entry.connect('activate', activated)
other_entry.connect('activate', activated)
other.show_all()
win.show_all()
entry.grab_focus()
win.present()
save()
Gtk.main()
