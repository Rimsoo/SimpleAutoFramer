#!/usr/bin/env bash
# Quick build + run for development (no system install).

set -Eeuo pipefail

BUILD_TYPE="${1:-Debug}"
BUILD_DIR="build"

echo "==> Syncing submodules"
git submodule update --init --recursive

# Prefer the distro OpenCV over any custom /usr/local build. A frequent
# pitfall on Arch: /usr/local/lib/cmake/opencv4/ pins an exact CUDA version
# ("Found unsuitable version 13.2, required exactly 12.8") which breaks the
# configure. /usr/lib/cmake/opencv4/ (opencv / opencv-cuda package) works.
OPENCV_DIR_ARG=()
if [ -z "${OpenCV_DIR:-}" ] && [ -f /usr/lib/cmake/opencv4/OpenCVConfig.cmake ]; then
    OPENCV_DIR_ARG=(-DOpenCV_DIR=/usr/lib/cmake/opencv4)
fi

mkdir -p "$BUILD_DIR"
pushd "$BUILD_DIR" >/dev/null

echo "==> Configuring ($BUILD_TYPE)"
cmake .. \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DINSTALL_MODE=OFF \
    -DUSE_CUDA=OFF \
    "${OPENCV_DIR_ARG[@]}"

echo "==> Building"
cmake --build . --parallel "$(nproc)"

echo "==> Running"
./simpleautoframer "${@:2}"

popd >/dev/null
