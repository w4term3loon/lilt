#!/usr/bin/env python3
"""Build local release archives and SHA-256 checksums; never publish or push."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile

from install_support import ROOT, copy_extension, copy_file, extension_metadata

SOURCE_FILES = ('CMakeLists.txt', 'README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md',
                '.gitignore', '.editorconfig', 'CONTRIBUTING.md', 'CHANGELOG.md')
SOURCE_DIRS = ('src', 'data', 'extension', 'scripts', 'tests', 'docs', 'LICENSES', '.github')
CPU_FEATURES = ('NATIVE', 'SSE42', 'AVX', 'AVX2', 'BMI2', 'FMA', 'F16C', 'AVX_VNNI',
                'AVX512', 'AVX512_VBMI', 'AVX512_VNNI', 'AVX512_BF16',
                'AMX_TILE', 'AMX_INT8', 'AMX_BF16')
WHISPER_REVISION = '7246b7311e089fe092c4abe7cfad5d0921f8be00'
RUNTIME_PACKAGES = ['libgtk-3-0t64', 'libpulse0', 'libgomp1', 'libstdc++6', 'libc6',
                    'libglib2.0-bin', 'python3']


def version():
    found = re.search(r'project\(lilt VERSION (\d+\.\d+\.\d+)', (ROOT / 'CMakeLists.txt').read_text())
    if not found:
        raise ValueError('Cannot read project version from CMakeLists.txt')
    return found.group(1)


def read_cache(build):
    values = {}
    for line in (build / 'CMakeCache.txt').read_text().splitlines():
        found = re.match(r'([^:#]+):[^=]+=(.*)', line)
        if found:
            values[found.group(1)] = found.group(2)
    return values


def validate_binary(binary, build):
    if not binary.is_file():
        raise ValueError('Build first: ./scripts/build.sh')
    if str(Path.home()).encode() + b'/' in binary.read_bytes():
        raise ValueError('Release executable contains a private home path; rebuild with the current CMake configuration')
    cache = read_cache(build)
    if cache.get('CMAKE_BUILD_TYPE') != 'Release':
        raise ValueError('Release archives require CMAKE_BUILD_TYPE=Release')
    for feature in CPU_FEATURES:
        if cache.get('GGML_' + feature, 'OFF').upper() not in ('OFF', 'FALSE', '0', ''):
            raise ValueError('Release archives require GGML_' + feature + '=OFF; run ./scripts/build.sh -DGGML_' + feature + '=OFF')
    for flag in ('CMAKE_C_FLAGS', 'CMAKE_CXX_FLAGS', 'CMAKE_C_FLAGS_RELEASE', 'CMAKE_CXX_FLAGS_RELEASE'):
        if re.search(r'-m(?:arch|cpu|tune)=native|/arch:|\s-m(?:avx|sse|fma|f16c)', cache.get(flag, '')):
            raise ValueError('CPU-specific compiler flags are not permitted in baseline releases: ' + flag)
    dynamic = subprocess.check_output(['readelf', '-d', str(binary)], text=True)
    if re.search(r'\((?:RPATH|RUNPATH)\)', dynamic):
        raise ValueError('Release executable contains RPATH/RUNPATH; rebuild with the current CMake configuration')
    if re.search(r'Shared library: \[lib(?:whisper|ggml)', dynamic):
        raise ValueError('whisper.cpp and ggml must be linked statically')
    environment = dict(os.environ)
    environment.pop('LD_LIBRARY_PATH', None)
    linked = subprocess.run(['ldd', str(binary)], env=environment, text=True, capture_output=True, check=True)
    if 'not found' in linked.stdout:
        raise ValueError('Executable cannot resolve its system runtime libraries:\n' + linked.stdout)
    actual_version = subprocess.check_output([str(binary), '--version'], env=environment, text=True, timeout=5).strip()
    if actual_version != 'lilt ' + version():
        raise ValueError('Executable version does not match CMakeLists.txt; rebuild before packaging')
    if extension_metadata().get('version-name') != version():
        raise ValueError('Extension version-name does not match the native release version')


def release_source_files():
    for name in SOURCE_FILES:
        yield ROOT / name
    for name in SOURCE_DIRS:
        for path in sorted((ROOT / name).rglob('*')):
            if not path.is_file() or path.is_symlink():
                continue
            relative = path.relative_to(ROOT)
            if '__pycache__' in relative.parts or path.suffix == '.pyc' or path.name == 'gschemas.compiled':
                continue
            yield path


def dependency_source_files(build):
    dependency = ROOT / 'vendor/whisper.cpp'
    if dependency.is_dir():
        for path in sorted(dependency.rglob('*')):
            if path.is_file() and '.git' not in path.relative_to(dependency).parts:
                yield path, path.relative_to(dependency)
        return
    dependency = build / '_deps/whisper-src'
    if not (dependency / '.git').exists():
        raise ValueError('Pinned whisper.cpp checkout missing; build from source before packaging')
    expected = subprocess.check_output(['git', '-C', str(dependency), 'rev-parse', WHISPER_REVISION + '^{commit}'], text=True).strip()
    actual = subprocess.check_output(['git', '-C', str(dependency), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != expected:
        raise ValueError('whisper.cpp checkout does not match the pinned source revision')
    changes = subprocess.check_output(['git', '-C', str(dependency), 'status', '--porcelain', '--untracked-files=no'], text=True)
    if changes.strip():
        raise ValueError('whisper.cpp contains modifications; release sources must match the pin')
    tracked = subprocess.check_output(['git', '-C', str(dependency), 'ls-files', '-z']).decode().split('\0')
    for name in tracked:
        if name:
            path = dependency / name
            if path.is_file():
                yield path, Path(name)


def add_tar_file(archive, source, name):
    info = archive.gettarinfo(str(source), arcname=str(name))
    # Normalize host ownership without modifying source files.
    info.uid = info.gid = 0
    info.uname = info.gname = 'root'
    if info.isfile():
        with source.open('rb') as content:
            archive.addfile(info, content)
    else:
        archive.addfile(info)


def package(build, output):
    if platform.system() != 'Linux' or platform.machine() != 'x86_64':
        raise ValueError('Native release archives currently target Linux x86_64 only')
    os_release = platform.freedesktop_os_release()
    if os_release.get('ID') != 'ubuntu' or os_release.get('VERSION_ID') != '24.04':
        raise ValueError('Build release binaries on Ubuntu 24.04 to match the supported runtime')
    binary = build / 'lilt'
    validate_binary(binary, build)
    # Resolve dependency files before creating any output, including pin checks.
    dependency_files = list(dependency_source_files(build))
    current_version = version()
    metadata = extension_metadata()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='lilt-package-') as temporary:
        work = Path(temporary)
        extension = work / 'extension'
        copy_extension(extension)
        extension_zip = output / f'lilt-{current_version}-extension.zip'
        with zipfile.ZipFile(extension_zip, 'w', zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(extension.rglob('*')):
                if path.is_file():
                    archive.write(path, path.relative_to(extension))
        bundle_name = f'lilt-{current_version}-ubuntu-24.04-x86_64'
        bundle = work / bundle_name
        for name in ('LICENSE', 'THIRD_PARTY_NOTICES.md', 'LICENSES/whisper.cpp-MIT.txt',
                     'data/io.github.lilt.Dictation.svg', 'scripts/install.py', 'scripts/install_support.py',
                     'scripts/uninstall.py', 'scripts/download-model.py'):
            copy_file(ROOT / name, bundle / name)
        copy_file(binary, bundle / 'bin/lilt', 0o755)
        shutil.copytree(extension, bundle / 'extension')
        (bundle / 'BUILD-INFO.json').write_text(json.dumps({
            'name': 'lilt', 'version': current_version, 'extension_uuid': metadata['uuid'],
            'extension_version': metadata['version'], 'platform': 'Ubuntu 24.04 x86_64',
            'cpu': 'baseline x86_64; GGML_NATIVE and explicit x86 ISA options disabled',
            'whisper_revision': WHISPER_REVISION, 'runtime_packages': RUNTIME_PACKAGES,
        }, indent=2) + '\n')
        (bundle / 'INSTALL.txt').write_text(
            'lilt ' + current_version + '\n\n'
            'Requires Ubuntu 24.04, GNOME Shell 46, and x86_64.\n'
            'Install runtime packages: sudo apt install ' + ' '.join(RUNTIME_PACKAGES) + '\n'
            'From this extracted directory, run:\n'
            '  python3 scripts/install.py\n'
            '  python3 scripts/download-model.py\n'
            'Log out and back in once so GNOME discovers the extension.\n'
            'Models download only when you request them.\n\n'
            'Stage without touching the desktop:\n'
            '  python3 scripts/install.py --prefix /usr/local --destdir /tmp/lilt-stage\n'
            'Uninstall (models/settings preserved):\n'
            '  python3 ~/.local/share/lilt/uninstall.py\n'
            'Add --purge-data to also remove settings and downloaded models.\n\n'
            'The matching source archive includes pinned whisper.cpp source.\n'
            'See LICENSE and THIRD_PARTY_NOTICES.md for license terms.\n')
        native_tar = output / (bundle_name + '.tar.gz')
        with tarfile.open(native_tar, 'w:gz') as archive:
            for path in sorted(bundle.rglob('*')):
                if path.is_file():
                    add_tar_file(archive, path, Path(bundle_name) / path.relative_to(bundle))
        source_tar = output / f'lilt-{current_version}-source.tar.gz'
        source_name = Path(f'lilt-{current_version}-source')
        with tarfile.open(source_tar, 'w:gz') as archive:
            for path in release_source_files():
                add_tar_file(archive, path, source_name / path.relative_to(ROOT))
            for path, relative in dependency_files:
                add_tar_file(archive, path, source_name / 'vendor/whisper.cpp' / relative)
    archives = [extension_zip, native_tar, source_tar]
    checksums = output / 'SHA256SUMS'
    lines = []
    for path in archives:
        with path.open('rb') as content:
            lines.append(hashlib.file_digest(content, 'sha256').hexdigest() + '  ' + path.name + '\n')
    checksums.write_text(''.join(lines))
    for path in [*archives, checksums]:
        print(path)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build')
    parser.add_argument('--output', type=Path, default=ROOT / 'dist')
    args = parser.parse_args(argv)
    try:
        package(args.build_dir.resolve(), args.output.resolve())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, 'Packaging failed: ' + str(error) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
