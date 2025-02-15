# SimpleAutoFramer C++ 

## Installation

### Prérequis

- OpenCV
- v4l2loopback
- CMake
- Make

### Instructions

Tested on Ubuntu 24.04 Gnome only.

```bash
sudo apt install v4l2loopback-dkms
sudo modprobe v4l2loopback devices=1 video_nr=20 card_label="AutoFrameCam"

sudo usermod -aG video $USER
newgrp video

git clone 
cd SimpleAutoFrame
chmod +x install.sh
sudo ./install.sh
simpleautoframe
```

## Utilisation

```bash
simpleautoframe
```

## TODO

- [x] Installation script
- [ ] XML UI
- [ ] Parametrable model
- [ ] GPU support
