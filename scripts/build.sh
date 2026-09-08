#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
for ren_tool in cmake ctest pkg-config git c++ make; do
    command -v "$ren_tool" >/dev/null || { printf 'Missing build tool: %s\n' "$ren_tool" >&2; exit 1; }
done
# Reset old developer caches to the portable defaults. Explicit arguments after
# these defaults still allow opt-in local tuning with -DGGML_NATIVE=ON.
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DGGML_NATIVE=OFF \
    -DGGML_SSE42=OFF -DGGML_AVX=OFF -DGGML_AVX2=OFF -DGGML_BMI2=OFF \
    -DGGML_FMA=OFF -DGGML_F16C=OFF "$@"
cmake --build build -j "${REN_BUILD_JOBS:-4}"
ctest --test-dir build --output-on-failure
