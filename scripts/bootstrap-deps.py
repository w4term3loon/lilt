#!/usr/bin/env python3
"""Unpack optional Ubuntu 24.04 x86_64 build dependencies locally, without sudo.

A working system C/C++ compiler, linker, make, Git, Python 3, and the matching
Ubuntu runtime libraries are prerequisites. This is not a general Linux sysroot.
"""

import concurrent.futures
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys


def main():
    release = platform.freedesktop_os_release()
    if release.get('ID') != 'ubuntu' or release.get('VERSION_ID') != '24.04' or platform.machine() != 'x86_64':
        sys.exit('This optional bootstrap supports Ubuntu 24.04 x86_64 only. Install build dependencies with your distribution package manager instead.')
    tools = ('apt-get', 'dpkg-deb', 'cc', 'c++', 'make', 'ld', 'git')
    missing = [name for name in tools if not shutil.which(name)]
    if missing:
        sys.exit('Missing prerequisite tools: ' + ', '.join(missing) + '. Install build-essential and git first; bootstrap does not install a compiler.')
    root = Path(__file__).resolve().parent.parent / '.deps'
    debs, sysroot = root / 'debs', root / 'sysroot'
    debs.mkdir(parents=True, exist_ok=True)
    sysroot.mkdir(parents=True, exist_ok=True)
    packages = ['libgtk-3-dev', 'libpulse-dev', 'cmake', 'pkg-config', 'libffi-dev']
    plan = subprocess.check_output(['apt-get', '-s', 'install', '--no-install-recommends', *packages], text=True)
    # Include requested packages even when apt considers them already installed.
    needed = sorted(set(packages + re.findall(r'^Inst (\S+)', plan, re.M)))
    print(f'Unpacking {len(needed)} Ubuntu packages into {sysroot}. System packages are unchanged.', flush=True)

    def fetch(package):
        process = subprocess.run(['apt-get', 'download', package], cwd=debs, text=True,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if process.returncode:
            raise RuntimeError(process.stdout)

    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        list(pool.map(fetch, needed))
    for deb in debs.glob('*.deb'):
        subprocess.run(['dpkg-deb', '-x', str(deb), str(sysroot)], check=True)
    # Development symlinks can point at the matching installed runtime library.
    for path in sysroot.rglob('*'):
        if path.is_symlink() and not path.exists():
            for directory in ('/usr/lib/x86_64-linux-gnu', '/lib/x86_64-linux-gnu'):
                runtime = Path(directory) / path.readlink().name
                if runtime.exists():
                    path.unlink()
                    path.symlink_to(runtime)
                    break
    print('Ready. Run ./scripts/build.sh. Keep .deps out of Git and release archives.')


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        sys.exit('Dependency bootstrap failed: ' + str(error))
