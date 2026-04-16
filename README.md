# SimpleAutoFramer C++

## Installation

### Prerequisites

- OpenCV
- V4l2loopback
- CMake
- Make
- Apindicator3
- Gtk3
- ...

Check the [install.sh](install.sh) script for more details.

### Instructions

Tested on Ubuntu 24.04 Gnome only.

## Install

```bash
git clone https://github.com/Rimsoo/SimpleAutoFramer.git
cd SimpleAutoFrame
chmod +x install.sh
sudo ./install.sh
simpleautoframe
```

## Usage

```bash
simpleautoframer
```

### Shortcuts

Click the keyboard icon in the *Shortcut* field of the UI (or double-click
the field) to capture a combination — the app will listen for the next
key press and fill the field for you. Supported modifiers: Ctrl, Alt,
Shift, Super. Special keys such as `KP_Add`, `KP_Subtract`, `F5`, … work
too. The raccourci format can still be typed by hand (e.g. `Ctrl+Alt+K`).

### Wayland / Hyprland — extra step required

Under Wayland SimpleAutoFramer uses `org.freedesktop.portal.GlobalShortcuts`
via `xdg-desktop-portal`. KDE and GNOME 47+ bind the keys automatically.
Under Hyprland you must also add one entry per shortcut to your
`hyprland.conf`, because the compositor only forwards keys it has been
explicitly asked to. Launch `simpleautoframer` once from a terminal: it
prints the exact lines to copy, e.g.:

```
bind = CTRL ALT, 1, global, :ctrl_alt_1
bind = CTRL ALT, 2, global, :ctrl_alt_2
```

**The leading `:` is mandatory** — xdg-desktop-portal registers non-flatpak
apps with an *empty* app_id, so the bind's third argument must literally
start with a colon (not `simpleautoframer:<id>`).

After editing the config, reload:

```bash
hyprctl reload
```

If a shortcut still doesn't fire, `xdg-desktop-portal-hyprland` is
probably routing to a stale session from a previous run. Clear the
portal's cache:

```bash
systemctl --user restart xdg-desktop-portal-hyprland
```

Make sure both services are running:

```bash
systemctl --user status xdg-desktop-portal xdg-desktop-portal-hyprland
```

You can inspect what the compositor knows via:

```bash
hyprctl globalshortcuts
```

Live shortcuts should show up with `app_id:` prefix matching the binds
(empty app_id → `:ctrl_alt_1 -> SimpleAutoFramer (…)`).

### Under X11

No extra step. The app uses `XGrabKey` directly.

## Unsintall

```bash
sudo simpleautoframe-uninstall
```

## TODO

- [x] Installation script
- [x] XML UI
- [x] Parametrable model
- [x] GPU support
- [x] Configuration file
- [x] Config shortcuts
- [ ] Add filters
