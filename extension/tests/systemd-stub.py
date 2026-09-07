"""Satisfy headless GNOME's XWayland startup dependency on a private test bus.

This does not start any user service. GetUnit deliberately reports no IBus unit,
so GNOME launches its own daemon inside the isolated environment.
"""
from gi.repository import Gio, GLib

XML = '''<node><interface name="org.freedesktop.systemd1.Manager">
<method name="GetUnit"><arg type="s" direction="in"/><arg type="o" direction="out"/></method>
<method name="StartUnit"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="o" direction="out"/></method>
<method name="StopUnit"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="o" direction="out"/></method>
<signal name="JobRemoved"><arg type="u"/><arg type="o"/><arg type="s"/><arg type="s"/></signal>
</interface></node>'''
sequence = 0


def method(connection, sender, path, interface, name, args, invocation):
    global sequence
    print(name, args.unpack(), flush=True)
    if name == 'GetUnit':
        invocation.return_dbus_error('org.freedesktop.systemd1.NoSuchUnit',
                                     'No test service exists.')
        return
    sequence += 1
    job = sequence
    unit = args.unpack()[0]
    job_path = f'/org/freedesktop/systemd1/job/{job}'
    invocation.return_value(GLib.Variant('(o)', (job_path,)))

    def complete():
        connection.emit_signal(None, '/org/freedesktop/systemd1',
                               'org.freedesktop.systemd1.Manager', 'JobRemoved',
                               GLib.Variant('(uoss)', (job, job_path, unit, 'done')))
        return GLib.SOURCE_REMOVE

    GLib.timeout_add(20, complete)


def ready(connection, name):
    connection.register_object('/org/freedesktop/systemd1',
                               Gio.DBusNodeInfo.new_for_xml(XML).interfaces[0],
                               method, None, None)


Gio.bus_own_name(Gio.BusType.SESSION, 'org.freedesktop.systemd1',
                 Gio.BusNameOwnerFlags.NONE, ready, None, None)
GLib.MainLoop().run()
