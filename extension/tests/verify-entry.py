"""Check actual GTK text/preedit state, not just extension actor visibility."""
import json
import pathlib
import sys

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
assert 'LILT TEST INLINE composition=true grab=false' in log, (root, log)
assert 'LILT TEST HELD composition=true session=true keys=2' in log, (root, log)
assert 'LILT TEST MODIFIER composition=true session=true keys=1' in log, (root, log)
assert 'LILT TEST TOGGLE HELD composition=true session=true keys=3' in log, (root, log)
assert all(f'LILT TEST {name} grab=false session=false inserting=false pulse=false' in log
           for name in ['FINISHED', 'TOGGLE FINISHED', 'CANCELED']), (root, log)
assert 'LILT TEST SCREENSHOT ERROR' not in log and 'LILT TEST FIXTURE ERROR' not in log, (root, log)
assert 'JS ERROR' not in log and 'has been already disposed' not in log, (root, log)
assert all((root / name).is_file() for name in ['recording.png', 'transcribing.png']), (root, log)
print(f'GNOME 46 {mode} integration passed: in-field revisions, selection, held Finish/repeat/modifiers, start-toggle finish, no submit, final once, cancellation, focus loss, input-source changes, target close. Logs: {root}')
