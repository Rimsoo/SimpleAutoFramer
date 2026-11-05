# SimpleAutoFramer

> [!NOTE]
> Main repository: [Forgejo](https://forgejo.aaaoz.fr/Rimso/SimpleAutoFramer) · Mirror: [GitHub](https://github.com/Rimsoo/SimpleAutoFramer) (kept in sync automatically).
> Issues and feedback are welcome on GitHub.

Open-source auto-framing for webcams (an alternative to NVIDIA Broadcast's
auto-frame). SimpleAutoFramer reads your webcam, tracks your face, crops and
zooms around it smoothly, and feeds the result to a **virtual camera**
(`v4l2loopback`) that any app can use: browsers, Zoom, Teams, Discord, OBS…

## Installation

### Arch Linux

```bash
cd packaging/arch
makepkg -si
```

(The same `PKGBUILD` is meant for the AUR as `simpleautoframer-git`.)

### Other distributions (Ubuntu/Debian, Fedora)

```bash
git clone --recursive https://forgejo.aaaoz.fr/Rimso/SimpleAutoFramer.git
cd SimpleAutoFramer
./install.sh
```

`install.sh` installs the dependencies, configures `v4l2loopback` to load at
boot as `/dev/video20` ("AutoFrameCam"), builds, and installs to `/usr`.
Run `./install.sh --help` for options. Uninstall with
`sudo simpleautoframer-uninstall`.

### Dependencies

CMake ≥ 3.20, a C++20 compiler, OpenCV ≥ 4.8 or 5 (with `xobjdetect` on 5),
gtkmm-3.0, ayatana-appindicator3, nlohmann-json, libX11/libXi, GLib/GIO,
and the `v4l2loopback` kernel module.

### Development build

```bash
./build_and_run.sh          # Debug build in ./build, then runs it
```

A dev build reads its data files and its config
(`simpleautoframer.config.json`) next to the binary. An installed build uses
`/usr/share/simpleautoframer` and `~/.config/simpleautoframer/`.

## Usage

```bash
simpleautoframer
```

Then select **AutoFrameCam** as the camera in your video app.

- Settings are grouped in **profiles**; sliders apply live, **Apply** saves.
- Closing the window keeps the app running in the tray. Launching
  `simpleautoframer` again brings the window back (handy when your desktop has
  no tray), and the tray menu's **Quit** exits.
- Two face detectors: **YuNet** (default, accurate; ONNX model from
  [opencv_zoo](https://github.com/opencv/opencv_zoo), MIT) and the older
  *Haar Cascade*. YuNet runs on the GPU when OpenCV has CUDA + cuDNN
  (e.g. Arch's `opencv-cuda`) and falls back to the CPU otherwise. It needs
  OpenCV ≥ 4.8; with older versions the app uses Haar.

If the virtual camera is missing:

```bash
sudo modprobe v4l2loopback devices=1 video_nr=20 card_label=AutoFrameCam exclusive_caps=1
```

### Shortcuts

Each profile can have a global shortcut that switches to it. Click the
keyboard icon in the *Shortcut* field (or double-click it) and press the
combination; modifiers Ctrl, Alt, Shift and Super are supported, as well as
keys like `KP_Add` or `F5`.

- **X11**: works out of the box (`XGrabKey`).
- **KDE / GNOME 47+ (Wayland)**: the shortcuts go through
  `xdg-desktop-portal`; accept the prompt on first use.
- **Hyprland**: the compositor only forwards keys that have a `global` bind,
  so SimpleAutoFramer writes those binds for you and runs `hyprctl reload`:
  - `hyprland.lua` (Hyprland ≥ 0.55): the binds go to
    `~/.config/hypr/simpleautoframer.lua`, loaded by a small managed block
    appended to `hyprland.lua`.
  - `hyprland.conf`: a managed block of `bind = …, global, :<id>` lines.

  Symlinked configs (dotfile managers) are written through. To manage the
  binds yourself, untick **Sync shortcuts to Hyprland config** in the menu
  (the managed block is then removed) and add for example:

  ```lua
  hl.bind("CTRL + ALT + 1", hl.dsp.global(":ctrl_alt_1"))
  ```

  The leading `:` is required: the portal registers non-flatpak apps with an
  empty app id. The ids are printed in the terminal when the app starts.
  If a shortcut still doesn't fire, restart the portal:
  `systemctl --user restart xdg-desktop-portal-hyprland`.

## CI

Builds run on Forgejo Actions (`.forgejo/workflows/build.yml`). Forgejo does
not provide hosted machines, so a **runner must be registered**; without one
the jobs wait forever with *"No matching online runner"*. See
[docs/CI.md](docs/CI.md).

## TODO

- [x] Installation script
- [x] XML UI
- [x] Parametrable model
- [x] GPU support
- [x] Configuration file
- [x] Config shortcuts
- [x] YuNet face detector (GPU when available)
- [ ] Add filters
