#!/usr/bin/env bash
set -euo pipefail

# Every desktop and input-service mutation below targets an isolated D-Bus/XDG
# session. Never launch an X11 fixture with an inherited host DISPLAY.
if [[ ${1:-} != --inside ]]; then
    lilt_extension_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
    lilt_extension_uuid=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["uuid"])' "$lilt_extension_dir/metadata.json")
    lilt_test_root=$(mktemp -d /tmp/lilt-shell-test.XXXXXX)
    lilt_test_mode=${1:-wayland}
    case "$lilt_test_mode" in wayland|ibus|x11) ;; *) printf 'Use wayland, ibus, or x11.\n' >&2; exit 2 ;; esac
    lilt_report_failure() {
        python3 - "$lilt_test_root" "$lilt_test_mode" <<'PY'
import os
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
mode = sys.argv[2]
markers = ', '.join(f'{name}={"yes" if (root / name).exists() else "no"}'
                    for name in ['display.json', 'entry-result.json', 'complete'])
summary = [f'GNOME {mode} integration failed ({markers}).']
print(summary[0])
for name in ['verify.log', 'entry.log', 'shell.log', 'systemd.log', 'session.log']:
    path = root / name
    if not path.is_file():
        continue
    with path.open('rb') as stream:
        stream.seek(max(0, path.stat().st_size - 32768))
        text = stream.read().decode('utf-8', errors='replace')
    for value, label in [(str(root), '<test>'), (str(Path.cwd()), '<checkout>'),
                         (str(Path.home()), '<home>')]:
        text = text.replace(value, label)
    text = re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]', '', text)
    text = ''.join(char for char in text if char >= ' ' or char in '\n\t')
    lines = [line[:500] for line in text.splitlines() if line.strip()]
    for line in lines[-30:]:
        # Prefix every synthetic log line so it cannot become a workflow command.
        print(f'[{name}] {line}')
    useful = [line for line in lines if re.search(
        r'error|fatal|exception|traceback|assert|failed|not found|no such|could not',
        line, re.IGNORECASE)]
    excerpt = list(dict.fromkeys(useful[-5:] + lines[-3:]))
    if excerpt:
        budget = 1400 if name == 'shell.log' else 650
        summary.append(f'{name}: ' + '\n'.join(excerpt)[:budget])
if os.environ.get('GITHUB_ACTIONS') == 'true':
    message = '\n'.join(summary)[:4500]
    message = message.replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')
    print(f'::error title=GNOME {mode} integration failed::{message}')
PY
    }
    lilt_install_dir="$lilt_test_root/data/gnome-shell/extensions/$lilt_extension_uuid"
    mkdir -p "$lilt_install_dir/schemas" "$lilt_test_root"/{config,cache,runtime}
    chmod 700 "$lilt_test_root/runtime"
    cp "$lilt_extension_dir"/{extension.js,composition.js,text.js,stylesheet.css,metadata.json} "$lilt_install_dir/"
    cp "$lilt_extension_dir/schemas/"*.xml "$lilt_install_dir/schemas/"
    cp -R "$lilt_extension_dir/tests/driver" "$lilt_test_root/data/gnome-shell/extensions/lilt-test@local"
    glib-compile-schemas --strict "$lilt_install_dir/schemas"
    env -u DISPLAY -u XAUTHORITY -u IBUS_ADDRESS -u IBUS_ADDRESS_FILE XDG_DATA_HOME="$lilt_test_root/data" XDG_CONFIG_HOME="$lilt_test_root/config" \
        XDG_CACHE_HOME="$lilt_test_root/cache" XDG_RUNTIME_DIR="$lilt_test_root/runtime" \
        WAYLAND_DISPLAY=lilt-smoke GNOME_SHELL_SESSION_MODE=user \
        LILT_SMOKE_ROOT="$lilt_test_root" LILT_EXTENSION_DIR="$lilt_extension_dir" LILT_EXTENSION_UUID="$lilt_extension_uuid" LILT_SMOKE_MODE="$lilt_test_mode" \
        dbus-run-session -- bash "$0" --inside >"$lilt_test_root/session.log" 2>&1 || {
            printf 'Integration test failed. Logs: %s\n' "$lilt_test_root"
            lilt_report_failure
            exit 1
        }
    if ! python3 "$lilt_extension_dir/tests/verify-entry.py" "$lilt_test_root" "$lilt_test_mode" >"$lilt_test_root/verify.log" 2>&1; then
        lilt_report_failure
        exit 1
    fi
    cat "$lilt_test_root/verify.log"
    exit
fi

python3 "$LILT_EXTENSION_DIR/tests/systemd-stub.py" >"$LILT_SMOKE_ROOT/systemd.log" 2>&1 &
lilt_stub_pid=$!
sleep 0.2
gsettings set org.gnome.shell enabled-extensions "['$LILT_EXTENSION_UUID', 'lilt-test@local']"
gnome-shell --mode=user --headless --wayland --virtual-monitor=1280x720 \
    --wayland-display=lilt-smoke >"$LILT_SMOKE_ROOT/shell.log" 2>&1 &
lilt_shell_pid=$!
lilt_entry_pid=
cleanup() {
    [[ -z "$lilt_entry_pid" ]] || kill "$lilt_entry_pid" 2>/dev/null || true
    kill "$lilt_shell_pid" "$lilt_stub_pid" 2>/dev/null || true
    sleep 0.5
    # Test compositors can hang while a private IBus daemon is disconnecting.
    # Escalation is limited to the PIDs launched by this harness.
    kill -KILL "$lilt_shell_pid" "$lilt_stub_pid" ${lilt_entry_pid:-} 2>/dev/null || true
    wait "$lilt_shell_pid" 2>/dev/null || true
}
trap cleanup EXIT
for lilt_attempt in {1..40}; do
    [[ ! -f "$LILT_SMOKE_ROOT/display.json" ]] || break
    sleep 0.2
done
python3 - >"$LILT_SMOKE_ROOT/entry.log" 2>&1 <<'PY' &
import json, os
from pathlib import Path
env = dict(os.environ)
if env['LILT_SMOKE_MODE'] == 'x11':
    values = json.loads((Path(env['LILT_SMOKE_ROOT']) / 'display.json').read_text())
    assert values['DISPLAY'] and values['XAUTHORITY'], values
    assert str(Path(values['XAUTHORITY']).parent) == env['XDG_RUNTIME_DIR'], values
    env.update(values)
    env['GDK_BACKEND'] = 'x11'
    env['GTK_IM_MODULE'] = 'ibus'
else:
    env['GDK_BACKEND'] = 'wayland'
    env['GTK_IM_MODULE'] = env['LILT_SMOKE_MODE']
os.execve('/usr/bin/python3', ['python3', env['LILT_EXTENSION_DIR'] + '/tests/entry.py'], env)
PY
lilt_entry_pid=$!
# Allow the timed scenarios plus seven bounded five-second IBus handovers.
for lilt_attempt in {1..300}; do
    [[ ! -f "$LILT_SMOKE_ROOT/complete" ]] || break
    sleep 0.2
done
[[ -f "$LILT_SMOKE_ROOT/complete" ]]
