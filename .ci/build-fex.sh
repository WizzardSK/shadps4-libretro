#!/bin/bash
# Builds FEX's static libraries for the aarch64 core's FEXCore guest CPU backend
# (ENABLE_FEX_GUEST_CPU), on an aarch64 machine, and its smoke test against them.
# Usage: .ci/build-fex.sh <source dir> <build dir>
set -euo pipefail

FEX_URL=https://github.com/FEX-Emu/FEX.git
# The revision the backend (src/core/fex, src/core/guest_cpu) was written against
FEX_REVISION=f2b679f6028ce1c38875233aecfcf5d3f8ebecec
HERE=$(cd "$(dirname "$0")" && pwd)
SOURCE=$1
BUILD=$2

if [ ! -d "$SOURCE/.git" ]; then
    git init -q "$SOURCE"
    git -C "$SOURCE" remote add origin "$FEX_URL"
fi
git -C "$SOURCE" fetch -q --depth 1 origin "$FEX_REVISION"
git -C "$SOURCE" checkout -q -f FETCH_HEAD
git -C "$SOURCE" submodule update --init --depth 1 --jobs 8 -- \
    External/unordered_dense External/rpmalloc External/xxhash External/fmt \
    External/range-v3 Source/Common/cpp-optparse
# FEXCore and an embedding smoke executable only (BUILD_FEXCORE_ONLY)
git -C "$SOURCE" apply "$HERE/fex/fexcore-only.patch"
# rpmalloc's thread heap with the default TLS model: its initial-exec one needs static TLS,
# which a module the frontend loads with dlopen does not get ("cannot allocate memory in
# static TLS block")
sed -i 's|^#define TLS_MODEL __attribute__((tls_model("initial-exec")))$|#define TLS_MODEL|' \
    "$SOURCE/External/rpmalloc/rpmalloc/rpmalloc.c"
grep -q '^#define TLS_MODEL __attribute__((tls_model("initial-exec")))$' \
    "$SOURCE/External/rpmalloc/rpmalloc/rpmalloc.c" && { echo "rpmalloc TLS model not changed" >&2; exit 1; }

cmake -S "$SOURCE" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="${CC:-clang}" \
    -DCMAKE_CXX_COMPILER="${CXX:-clang++}" \
    -DTUNE_CPU=none \
    -DBUILD_FEXCORE_ONLY=ON \
    -DFEXCORE_SMOKE_SOURCE="$HERE/fex/fexcore-smoke.cpp" \
    -DBUILD_TESTING=OFF \
    -DBUILD_FEX_LINUX_TESTS=OFF \
    -DBUILD_THUNKS=OFF \
    -DBUILD_FEXCONFIG=OFF \
    -DENABLE_GDB_SYMBOLS=OFF \
    -DENABLE_LTO=OFF \
    -DENABLE_JEMALLOC_GLIBC_ALLOC=OFF \
    -DENABLE_OFFLINE_TELEMETRY=OFF \
    -DENABLE_VIXL_DISASSEMBLER=OFF \
    -DENABLE_VIXL_SIMULATOR=OFF \
    -DENABLE_ZYDIS=OFF \
    -DENABLE_FEXCORE_PROFILER=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake --build "$BUILD" --target fexcore-smoke --parallel
