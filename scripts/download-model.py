#!/usr/bin/env python3
"""Download a pinned Whisper GGML model, checking its complete SHA-256 digest.

Only Python's standard library is needed. The application itself does not use
Python or a network connection. Files are published only after verification.
"""

import argparse
import hashlib
import os
from pathlib import Path
import signal
import sys
import tempfile
import time
import urllib.error
import urllib.request


REVISION = "98aa99a0a9db05ae2342309f5096248665f7cba3"
BASE_URL = "https://huggingface.co/ggerganov/whisper.cpp/resolve/" + REVISION
# Byte counts and SHA-256 LFS object IDs from the publisher's pinned manifest:
# https://huggingface.co/api/models/ggerganov/whisper.cpp/tree/
# 98aa99a0a9db05ae2342309f5096248665f7cba3?expand=true&limit=100
MODELS = {
    "medium.en-q5_0": (539225533, "76733e26ad8fe1c7a5bf7531a9d41917b2adc0f20f2e4f5531688a8c6cd88eb0"),
    "small.en-q5_1": (190098681, "bfdff4894dcb76bbf647d56263ea2a96645423f1669176f4844a1bf8e478ad30"),
    "base.en-q5_1": (59721011, "4baf70dd0d7c4247ba2b81fafd9c01005ac77c2f9ef064e00dcf195d0e2fdd2f"),
    "tiny.en-q5_1": (32166155, "c77c5766f1cef09b6b7d47f21b546cbddd4157886b3b5d6d4f709e91e66c7c2b"),
}
CHUNK_SIZE = 1024 * 1024


def default_directory():
    data_home = Path(os.environ.get("XDG_DATA_HOME", ""))
    if not data_home.is_absolute():
        data_home = Path.home() / ".local" / "share"
    return data_home / "lilt" / "models"


def verified(path, expected_size, expected_sha256):
    if not path.is_file() or path.stat().st_size != expected_size:
        return False
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(CHUNK_SIZE), b""):
            digest.update(chunk)
    return digest.hexdigest() == expected_sha256


def download(model, directory, force=False):
    expected_size, expected_sha256 = MODELS[model]
    filename = "ggml-" + model + ".bin"
    directory = directory.expanduser().absolute()
    directory.mkdir(parents=True, exist_ok=True)
    target = directory / filename
    if target.exists() or target.is_symlink():
        if verified(target, expected_size, expected_sha256):
            print("Verified existing model: " + str(target))
            return target
        if not force:
            raise ValueError(str(target) + " exists but fails verification. Use --force to replace it.")

    request = urllib.request.Request(
        BASE_URL + "/" + filename,
        headers={"User-Agent": "lilt-model-downloader/1.0"},
    )
    temporary_path = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb", dir=directory, prefix="." + filename + ".", suffix=".part", delete=False
        ) as output:
            temporary_path = Path(output.name)
            digest = hashlib.sha256()
            downloaded = 0
            last_report = 0.0
            print("Downloading " + filename + " (" + str(round(expected_size / 1048576)) + " MiB)…", flush=True)
            with urllib.request.urlopen(request, timeout=30) as response:
                if response.geturl().split(":", 1)[0] != "https":
                    raise ValueError("Download redirected to an insecure URL.")
                for chunk in iter(lambda: response.read(CHUNK_SIZE), b""):
                    downloaded += len(chunk)
                    if downloaded > expected_size:
                        raise ValueError("Download exceeds the pinned model size.")
                    digest.update(chunk)
                    output.write(chunk)
                    now = time.monotonic()
                    if now - last_report >= 2.0:
                        print("  " + str(round(downloaded * 100 / expected_size)) + "%", flush=True)
                        last_report = now
            if downloaded != expected_size or digest.hexdigest() != expected_sha256:
                raise ValueError("Downloaded model failed size or SHA-256 verification; no model installed.")
            output.flush()
            os.fsync(output.fileno())
        if force:
            os.replace(temporary_path, target)
        else:
            try:
                # Publish without clobbering a file another downloader created.
                os.link(temporary_path, target)
            except FileExistsError:
                if not verified(target, expected_size, expected_sha256):
                    raise ValueError("Destination changed during download; existing file was preserved.")
        print("Verified model: " + str(target))
        return target
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def interrupted(_signum, _frame):
    raise KeyboardInterrupt


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model", nargs="?", choices=MODELS, default="small.en-q5_1")
    parser.add_argument("--directory", type=Path, default=default_directory(), help="model directory")
    parser.add_argument("--list", action="store_true", help="show supported models without downloading")
    parser.add_argument("--force", action="store_true", help="replace an existing model that fails verification")
    args = parser.parse_args(argv)
    if args.list:
        for model, (size, _) in MODELS.items():
            print(f"{model:19} {size / 1048576:6.1f} MiB  English")
        return 0
    signal.signal(signal.SIGTERM, interrupted)
    try:
        download(args.model, args.directory, args.force)
    except KeyboardInterrupt:
        print("Download cancelled; temporary file removed.", file=sys.stderr)
        return 130
    except (OSError, ValueError, urllib.error.URLError) as error:
        print("Model download failed: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
