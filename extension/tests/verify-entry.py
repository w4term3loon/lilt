"""Check actual GTK text/preedit state, not just extension actor visibility."""
import json
import pathlib
import sys
import gi

gi.require_version('GdkPixbuf', '2.0')
from gi.repository import GdkPixbuf

root = pathlib.Path(sys.argv[1])
mode = sys.argv[2]
result = json.loads((root / 'entry-result.json').read_text())
snapshots = result['snapshots']
original = 'before REPLACE after'
final = 'before Clear words 🦉 Hello. after'
for name in ['draft', 'revised', 'held', 'modifier', 'toggle-held', 'enter-held', 'cancel', 'focus-lost', 'engine-change']:
    assert snapshots[name]['text'] == original, (root, name, snapshots[name])
    assert snapshots[name]['selection'] == [7, 14], (root, name, snapshots[name])
for name in ['draft', 'revised']:
    assert snapshots[name]['preedit'].startswith('Clear <words> 🦉 & '), (root, name, snapshots[name])
    attributes = snapshots[name]['layout_attributes']
    if mode in ['ibus', 'x11']:
        assert 'underline none' in attributes and 'underline single' not in attributes, (root, name, attributes)
        assert 'foreground #4fff80ff5fff' in attributes, (root, name, attributes)
    else:
        # GNOME 46 forwards only text; GTK's Wayland module draws its own underline.
        assert 'underline single' in attributes and 'foreground' not in attributes, (root, name, attributes)
for name in ['final', 'toggle-final', 'enter-final']:
    assert snapshots[name]['text'] == final, (root, name, snapshots[name])
    assert snapshots[name]['preedit'] == '', (root, name, snapshots[name])
    assert snapshots[name]['engine'] == 'xkb:us::eng', (root, name, snapshots[name])
for name in ['cancel', 'focus-lost', 'target-closed']:
    assert snapshots[name]['preedit'] == '', (root, name, snapshots[name])
    assert snapshots[name]['other_text'] == '', (root, name, snapshots[name])
    assert snapshots[name]['engine'] == 'xkb:us::eng', (root, name, snapshots[name])
assert snapshots['engine-change']['engine'] == 'xkb:gb::eng', (root, result)
assert snapshots['engine-change']['preedit'] == '' and result['engine_switch_ok'], (root, result)
assert all(item['Activate'] == 0 for item in snapshots.values()), (root, result)
assert result['Stop'] == 3 and result['Cancel'] == 4, (root, result)
assert result['Toggle'] == 7 and result['Attach'] >= 1, (root, result)
assert any('draft' in item for item in result['preedit_events']), (root, result)
assert any('revised' in item for item in result['preedit_events']), (root, result)
log = (root / 'shell.log').read_text(errors='replace')
assert 'LILT TEST WRONG KEYS state=recording' in log, (root, log)
assert 'LILT TEST LEVEL changed=true' in log, (root, log)
assert 'LILT TEST CAPS unscaled=true' in log, (root, log)
assert 'LILT TEST DOTS visible=true wave=false count=3 moving=true' in log, (root, log)
assert 'LILT TEST INLINE composition=true grab=false' in log, (root, log)
assert 'LILT TEST HELD composition=true session=true keys=2' in log, (root, log)
assert 'LILT TEST MODIFIER composition=true session=true keys=1' in log, (root, log)
assert 'LILT TEST TOGGLE HELD composition=true session=true keys=3' in log, (root, log)
assert all(f'LILT TEST {name} grab=false session=false inserting=false dots=false resting=true' in log
           for name in ['FINISHED', 'TOGGLE FINISHED', 'CANCELED']), (root, log)
assert 'LILT TEST SCREENSHOT ERROR' not in log and 'LILT TEST FIXTURE ERROR' not in log, (root, log)
assert 'JS ERROR' not in log and 'has been already disposed' not in log, (root, log)
assert all((root / name).is_file() for name in ['recording.png', 'transcribing.png']), (root, log)
# Actor visibility alone missed a real stylesheet regression. Verify that every
# recording bar and finalization dot paints visible pixels in the screenshot.
frames = json.loads((root / 'indicator-frames.json').read_text())
for label, kind in [('recording', 'bars'), ('transcribing', 'dots')]:
    pixbuf = GdkPixbuf.Pixbuf.new_from_file(str(root / f'{label}.png'))
    pixels = pixbuf.get_pixels()
    channels = pixbuf.get_n_channels()
    stride = pixbuf.get_rowstride()
    frame = frames[label]
    scale = pixbuf.get_width() / frame['stageWidth']
    for index, bounds in enumerate(frame[kind]):
        x = round((bounds['x'] + bounds['width'] / 2) * scale)
        y = round((bounds['y'] + bounds['height'] / 2) * scale)
        brightness = []
        radius = 6 if kind == 'dots' else 2
        for py in range(max(0, y - radius), min(pixbuf.get_height(), y + radius + 1)):
            offset = py * stride + x * channels
            brightness.append(min(pixels[offset:offset + 3]))
        assert max(brightness) > 100, (root, label, index, 'indicator has no visible paint', brightness)
print(f'GNOME 46 {mode} integration passed: in-field revisions and styling, visible rounded bars, animated dots, selection, held Finish/repeat/modifiers, start-toggle finish, no submit, final once, cancellation, focus loss, input-source changes, target close. Logs: {root}')
