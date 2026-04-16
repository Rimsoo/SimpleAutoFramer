#!/usr/bin/env bash
#
# SimpleAutoFramer — installation script
# Supports: Ubuntu/Debian, Arch/Manjaro, Fedora.
# For Arch users: prefer the AUR package `simpleautoframer-git`.
#
# Usage:
#   ./install.sh                  # configure + build + install system-wide
#   ./install.sh --no-install     # configure + build only (no sudo install)
#   ./install.sh --no-deps        # skip package-manager dependency install
#   ./install.sh --no-modprobe    # skip loading v4l2loopback right now
#
# This script is idempotent: re-running it rebuilds cleanly without breaking
# an existing installation.

set -Eeuo pipefail

INSTALL=1
INSTALL_DEPS=1
LOAD_MODULE=1

for arg in "$@"; do
    case "$arg" in
        --no-install)   INSTALL=0 ;;
        --no-deps)      INSTALL_DEPS=0 ;;
        --no-modprobe)  LOAD_MODULE=0 ;;
        -h|--help)
            sed -n '2,18p' "$0"; exit 0 ;;
        *) echo "Unknown option: $arg" >&2; exit 2 ;;
    esac
done

# ------------------------------------------------------------------
# Resolve the real invoking user (so that `sudo ./install.sh` doesn't
# mis-identify the user as root when we usermod -aG video).
# ------------------------------------------------------------------
REAL_USER="${SUDO_USER:-$USER}"
if [[ "$REAL_USER" == "root" ]]; then
    echo "!! Running as root without SUDO_USER. usermod -aG video will be skipped."
fi

# ------------------------------------------------------------------
# Detect distribution
# ------------------------------------------------------------------
DISTRO="unknown"
if [[ -r /etc/os-release ]]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    DISTRO="${ID_LIKE:-$ID}"
fi

pm_install() {
    case "$DISTRO" in
        *debian*|*ubuntu*)
            sudo apt-get update
            sudo apt-get install -y \
                build-essential cmake git pkg-config \
                nlohmann-json3-dev libopencv-dev \
                libgtkmm-3.0-dev libayatana-appindicator3-dev \
                libx11-dev libxi-dev libglib2.0-dev \
                v4l2loopback-dkms
            ;;
        *arch*|*manjaro*|*endeavouros*)
            sudo pacman -S --needed --noconfirm \
                base-devel cmake git pkgconf \
                nlohmann-json opencv \
                gtkmm3 libayatana-appindicator \
                libx11 libxi glib2 \
                v4l2loopback-dkms
            ;;
        *fedora*|*rhel*)
            sudo dnf install -y \
                gcc-c++ cmake git pkgconf-pkg-config \
                json-devel opencv-devel \
                gtkmm30-devel libayatana-appindicator-gtk3-devel \
                libX11-devel libXi-devel glib2-devel \
                v4l2loopback
            ;;
        *)
            echo "!! Unsupported distro '$DISTRO'. Install deps manually."
            echo "   Required: cmake, OpenCV, gtkmm-3.0, ayatana-appindicator3,"
            echo "             nlohmann_json, libX11, libXi, glib2, v4l2loopback."
            ;;
    esac
}

# ------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------
if (( INSTALL_DEPS )); then
    echo "==> Installing system dependencies…"
    pm_install
fi

# ------------------------------------------------------------------
# Submodules
# ------------------------------------------------------------------
echo "==> Syncing git submodules…"
git submodule update --init --recursive

# ------------------------------------------------------------------
# v4l2loopback — persistent configuration
# ------------------------------------------------------------------
echo "==> Configuring v4l2loopback…"
sudo install -Dm0644 /dev/stdin /etc/modules-load.d/v4l2loopback.conf <<EOF
# Loaded by SimpleAutoFramer
v4l2loopback
EOF

sudo install -Dm0644 /dev/stdin /etc/modprobe.d/v4l2loopback.conf <<EOF
# Created by SimpleAutoFramer install.sh
options v4l2loopback devices=1 video_nr=20 card_label="AutoFrameCam" exclusive_caps=1
EOF

if (( LOAD_MODULE )); then
    if ! lsmod | grep -q '^v4l2loopback '; then
        echo "==> Loading v4l2loopback now…"
        sudo modprobe v4l2loopback \
            devices=1 video_nr=20 card_label="AutoFrameCam" exclusive_caps=1 || {
                echo "!! modprobe failed — did DKMS build succeed?"
                echo "   Check:  sudo dkms status v4l2loopback"
            }
    fi
fi

# ------------------------------------------------------------------
# Group membership
# ------------------------------------------------------------------
if [[ "$REAL_USER" != "root" ]]; then
    if ! id -nG "$REAL_USER" | grep -qw video; then
        echo "==> Adding $REAL_USER to group 'video'…"
        sudo usermod -aG video "$REAL_USER"
        echo "   You must log out and back in for this to take effect."
    fi
fi

# ------------------------------------------------------------------
# Build
# ------------------------------------------------------------------
BUILD_DIR="build"
echo "==> Configuring CMake (Release)…"
mkdir -p "$BUILD_DIR"
pushd "$BUILD_DIR" >/dev/null

# Prefer the distro OpenCV over any custom /usr/local build (see build_and_run.sh).
OPENCV_DIR_ARG=()
if [ -z "${OpenCV_DIR:-}" ] && [ -f /usr/lib/cmake/opencv4/OpenCVConfig.cmake ]; then
    OPENCV_DIR_ARG=(-DOpenCV_DIR=/usr/lib/cmake/opencv4)
fi

cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DINSTALL_MODE=ON \
    -DUSE_X11_SHORTCUTS=ON \
    -DUSE_PORTAL_SHORTCUTS=ON \
    -DUSE_CUDA=OFF \
    "${OPENCV_DIR_ARG[@]}"

echo "==> Building…"
cmake --build . --parallel "$(nproc)"

# ------------------------------------------------------------------
# Install
# ------------------------------------------------------------------
if (( INSTALL )); then
    echo "==> Installing to /usr…"
    sudo cmake --install .

    # Refresh icon + desktop caches
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        sudo gtk-update-icon-cache -q /usr/share/icons/hicolor || true
    fi
    if command -v update-desktop-database >/dev/null 2>&1; then
        sudo update-desktop-database -q /usr/share/applications || true
    fi
else
    echo "==> --no-install: skipping system install. Binary is at $PWD/simpleautoframer"
fi

popd >/dev/null

echo ""
echo "Done. Next steps:"
echo "  * If you were added to the 'video' group, log out and back in."
echo "  * Under Wayland, the first launch will prompt xdg-desktop-portal"
echo "    to bind your global shortcuts — accept it."
echo "  * Run:   simpleautoframer"
