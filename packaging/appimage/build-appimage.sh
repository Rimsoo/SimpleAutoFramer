#!/usr/bin/env bash
# Build SimpleAutoFramer-<version>-x86_64.AppImage into ./dist.
#
# Meant to run as root in a fresh ubuntu:22.04 container (see
# .forgejo/workflows/release.yml): an old glibc base keeps the AppImage
# runnable on most distributions. Ubuntu 22.04 ships OpenCV 4.5, too old for
# the YuNet model, so a minimal OpenCV is built from source.
#
# Usage: packaging/appimage/build-appimage.sh [version]

set -Eeuo pipefail

VERSION="${1:-$(sed -n 's/^project(simpleautoframer VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)}"
OPENCV_VERSION=4.10.0
JOBS="$(nproc)"
ROOT="$PWD"
WORK="$ROOT/.appimage-build"
APPDIR="$WORK/AppDir"
export DEBIAN_FRONTEND=noninteractive
export APPIMAGE_EXTRACT_AND_RUN=1   # no FUSE inside containers

echo "==> Dependencies"
apt-get update
apt-get install -y --no-install-recommends \
    ca-certificates curl file git build-essential cmake ninja-build pkg-config \
    libjpeg-dev libpng-dev \
    nlohmann-json3-dev libgtkmm-3.0-dev libayatana-appindicator3-dev \
    libx11-dev libxi-dev libglib2.0-dev \
    librsvg2-common libgdk-pixbuf2.0-bin

mkdir -p "$WORK"

echo "==> OpenCV $OPENCV_VERSION (minimal)"
if [ ! -f /usr/local/lib/cmake/opencv4/OpenCVConfig.cmake ]; then
    curl -fsSL "https://github.com/opencv/opencv/archive/refs/tags/$OPENCV_VERSION.tar.gz" \
        | tar -xz -C "$WORK"
    cmake -S "$WORK/opencv-$OPENCV_VERSION" -B "$WORK/opencv-build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DBUILD_LIST=core,imgproc,imgcodecs,videoio,objdetect,dnn \
        -DBUILD_SHARED_LIBS=ON \
        -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF \
        -DBUILD_opencv_apps=OFF -DBUILD_DOCS=OFF -DBUILD_JAVA=OFF \
        -DBUILD_opencv_python3=OFF \
        -DWITH_V4L=ON -DWITH_FFMPEG=OFF -DWITH_GSTREAMER=OFF \
        -DWITH_GTK=OFF -DWITH_QT=OFF -DWITH_OPENCL=OFF -DWITH_IPP=OFF \
        -DWITH_TIFF=OFF -DWITH_WEBP=OFF -DWITH_OPENEXR=OFF -DWITH_JASPER=OFF \
        -DWITH_OPENJPEG=OFF -DWITH_1394=OFF -DWITH_ADE=OFF -DWITH_EIGEN=OFF \
        -DWITH_LAPACK=OFF -DWITH_ITT=OFF -DWITH_PROTOBUF=ON -DBUILD_PROTOBUF=ON
    cmake --build "$WORK/opencv-build" --parallel "$JOBS"
    cmake --install "$WORK/opencv-build"
    ldconfig
fi

echo "==> SimpleAutoFramer $VERSION"
rm -rf "$WORK/build" "$APPDIR"
cmake -S "$ROOT" -B "$WORK/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DINSTALL_MODE=ON \
    -DINSTALL_UNINSTALLER=OFF \
    -DOpenCV_DIR=/usr/local/lib/cmake/opencv4
cmake --build "$WORK/build" --parallel "$JOBS"
DESTDIR="$APPDIR" cmake --install "$WORK/build"

echo "==> linuxdeploy"
cd "$WORK"
[ -x linuxdeploy-x86_64.AppImage ] || curl -fsSLO \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
[ -x linuxdeploy-plugin-gtk.sh ] || curl -fsSLO \
    https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh
chmod +x linuxdeploy-x86_64.AppImage linuxdeploy-plugin-gtk.sh

export DEPLOY_GTK_VERSION=3
export LD_LIBRARY_PATH=/usr/local/lib
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"
export OUTPUT="SimpleAutoFramer-$VERSION-x86_64.AppImage"
PATH="$WORK:$PATH" ./linuxdeploy-x86_64.AppImage \
    --appdir "$APPDIR" \
    --desktop-file "$APPDIR/usr/share/applications/org.simple_auto_framer.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/64x64/apps/simpleautoframer.png" \
    --plugin gtk \
    --output appimage

mkdir -p "$ROOT/dist"
mv "$OUTPUT" "$ROOT/dist/"
chmod +x "$ROOT/dist/$OUTPUT"

echo "==> Smoke test"
"$ROOT/dist/$OUTPUT" --version

echo "==> Done: dist/$OUTPUT"
