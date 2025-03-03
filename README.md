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

For the shortcuts modifiers, use: ctrl+alt+shift+super+K  
For the keypad shortcuts, use: KP_Add, KP_Subtract, KP_Multiply, KP_0, KP_1...

```bash
simpleautoframe
```

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
