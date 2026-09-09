#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Package a committed release locally. Nothing is uploaded."""

import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parent.parent


def package():
    if subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True).strip():
        raise ValueError('Commit the release changes before packaging.')
    metadata = json.loads((ROOT / 'extension/metadata.json').read_text())
    version = metadata['version-name']
    project = (ROOT / 'CMakeLists.txt').read_text()
    if not re.fullmatch(r'\d+\.\d+\.\d+', version) or len(version) > 16 or f'VERSION {version} ' not in project:
        raise ValueError('CMake and extension release versions must match.')
    if not re.fullmatch(r'[A-Za-z0-9_.-]+@[A-Za-z0-9_.-]+', metadata['uuid']):
        raise ValueError('Invalid extension UUID.')
    subprocess.run(['glib-compile-schemas', '--strict', '--dry-run',
                    str(ROOT / 'extension/schemas')], check=True)
    destination = ROOT / 'dist'
    destination.mkdir(exist_ok=True)
    extension = destination / f"{metadata['uuid']}.shell-extension.zip"
    # EGO assigns its own numeric version. No binary, model, or local data belongs here.
    metadata.pop('version', None)
    with zipfile.ZipFile(extension, 'w', zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('metadata.json', json.dumps(metadata, indent=2) + '\n')
        for name in ('extension.js', 'composition.js', 'orb.js', 'text.js',
                     'stylesheet.css', 'wren-symbolic.svg',
                     'schemas/org.gnome.shell.extensions.ren.gschema.xml'):
            archive.write(ROOT / 'extension' / name, name)
        archive.write(ROOT / 'LICENSE', 'LICENSE')
    source = destination / f'ren-{version}.tar.gz'
    subprocess.run(['git', 'archive', '--format=tar.gz', f'--prefix=ren-{version}/',
                    f'--output={source}', 'HEAD'], cwd=ROOT, check=True)
    checksums = destination / 'SHA256SUMS'
    checksums.write_text(''.join(f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n'
                                for path in (extension, source)))
    for path in (extension, source, checksums):
        print(path.relative_to(ROOT))


if __name__ == '__main__':
    try:
        package()
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        sys.exit(f'Packaging failed: {error}')
