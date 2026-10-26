#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CMAKE_BIN="${CMAKE_BIN:-/tmp/toolchain/venv/bin/cmake}"
NINJA_BIN="${NINJA_BIN:-/tmp/toolchain/venv/bin/ninja}"
WRAP_DIR="/tmp/emesis_zig_wrappers"
BUILD_ROOT="/tmp/emesis_build"
OUT_JNILIBS="${EMESIS_JNILIBS_OUT:-${ROOT_DIR}/app/src/main/jniLibs}"
EXT_DEPS_DIR="${EMESIS_EXT_DEPS_DIR:-/tmp/refs/legacy_src/3rdparty}"

"${ROOT_DIR}/tools/setup_zig_wrappers.sh"
/tmp/toolchain/venv/bin/python "${ROOT_DIR}/tools/gen_gl_forwards.py"

ABIS=("armeabi-v7a" "arm64-v8a" "x86" "x86_64")
JOBS="${EMESIS_BUILD_JOBS:-2}"

for abi in "${ABIS[@]}"; do
    echo "============================================================"
    echo ">>> Building Emesis Renderer Native Engines for ABI: ${abi}"
    echo "============================================================"
    bdir="${BUILD_ROOT}/${abi}"
    mkdir -p "${bdir}" "${OUT_JNILIBS}/${abi}"

    "${CMAKE_BIN}" -S "${ROOT_DIR}/native" -B "${bdir}" -G Ninja \
        -DCMAKE_MAKE_PROGRAM="${NINJA_BIN}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_SYSTEM_NAME=Linux \
        -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
        -DANDROID=ON \
        -DEMESIS_EXT_DEPS_DIR="${EXT_DEPS_DIR}" \
        -DCMAKE_C_COMPILER="${WRAP_DIR}/${abi}-cc" \
        -DCMAKE_CXX_COMPILER="${WRAP_DIR}/${abi}-cxx" \
        -DCMAKE_AR="${WRAP_DIR}/${abi}-ar" \
        -DCMAKE_RANLIB="${WRAP_DIR}/${abi}-ranlib"

    "${NINJA_BIN}" -C "${bdir}" -j "${JOBS}" emesis_modern emesis_legacy emesis_renderer

    cp -f "${bdir}/emesis_modern/libemesis_modern.so"     "${OUT_JNILIBS}/${abi}/libemesis_modern.so"
    cp -f "${bdir}/emesis_legacy/libemesis_legacy.so"     "${OUT_JNILIBS}/${abi}/libemesis_legacy.so"
    cp -f "${bdir}/emesis_router/libemesis_renderer.so"   "${OUT_JNILIBS}/${abi}/libemesis_renderer.so"

    ls -lh "${OUT_JNILIBS}/${abi}/"*.so
    rm -rf "${bdir}"
done

echo "=== ALL 12 NATIVE .SO BINARIES BUILT SUCCESSFULLY ==="
