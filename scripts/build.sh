#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
for ren_tool in cmake ctest pkg-config git c++ make; do
    command -v "$ren_tool" >/dev/null || { printf 'Missing build tool: %s\n' "$ren_tool" >&2; exit 1; }
done
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build build -j "${REN_BUILD_JOBS:-4}"
ctest --test-dir build --output-on-failure
