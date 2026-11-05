#!/usr/bin/env bash
# Quick build + run for development (no system install).

set -Eeuo pipefail

BUILD_TYPE="${1:-Debug}"
BUILD_DIR="build"

echo "==> Syncing submodules"
git submodule update --init --recursive


mkdir -p "$BUILD_DIR"
pushd "$BUILD_DIR" >/dev/null

echo "==> Configuring ($BUILD_TYPE)"
cmake .. \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DINSTALL_MODE=OFF

echo "==> Building"
cmake --build . --parallel "$(nproc)"

echo "==> Running"
./simpleautoframer "${@:2}"

popd >/dev/null
