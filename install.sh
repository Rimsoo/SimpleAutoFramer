#!/bin/bash

# Verifier le sudo
if [ "$EUID" -ne 0 ]; then
    cd ..
    echo "This script requires administrator rights to install the program."
    echo "Run: sudo ./install.sh"
    exit 1
fi

echo "Installing dependencies..."
apt update || { echo "Error while updating the package list"; exit 1; }
apt install build-essential cmake nlohmann-json3-dev libgtkmm-3.0-dev libayatana-appindicator3-dev libtbbmalloc2 libgtk-3-dev v4l2loopback-dkms libboost-all-dev || { echo "Error while installing dependencies"; exit 1; }

# Install OpenCV If you don't have it already installed with CUDA support
# apt install libopencv-dev || { echo "Error while installing OpenCV"; exit 1; }

# Load the v4l2loopback module
modprobe v4l2loopback devices=1 video_nr=20 card_label="AutoFrameCam"
usermod -aG video $USER

# Create build directory if it doesn't exist
if [ ! -d "build" ]; then
    echo "Création du dossier build..."
    mkdir build
fi

cd build

echo "Configuring project..."
cmake .. -DINSTALL_MODE=ON || { echo "Error while configuring the project"; exit 1; }

echo "Compiling project..."
make || { echo "Error while compiling the project"; exit 1; }

echo "Installing project..."
make install || { echo "Error while installing the project"; exit 1; }
chmod +x /usr/bin/simpleautoframer
chmod +x /usr/bin/simpleautoframer-uninstall

gtk-update-icon-cache /usr/share/icons/hicolor
cd ..

# Delete the build directory
rm -rf build

echo "Installation completed successfully."
echo "You can now run the program by searching for 'SimpleAutoFramer' in the application menu."
echo "Or by running the command: simpleautoframer"
echo "To uninstall the program, run the command: sudo simpleautoframer-uninstall"
# Lancer l'exécutable installé
# echo "Running the program..."
# # newgrp video
# /usr/local/bin/simpleautoframer || { echo "Error while running the program"; exit 1; }