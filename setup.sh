#!/bin/bash

# Installer les dépendances système
sudo apt update
sudo apt install -y \
    python3-pip \
    python3-tk \
    python3-gi \
    gir1.2-appindicator3-0.1 \
    v4l2loopback-dkms \
    libopencv-dev \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly

# Installer les dépendances Python
pip3 install \
    pyinstaller \
    opencv-python \
    mediapipe \
    pyvirtualcam \
    pillow \
    numpy \
    PyGObject

# Builder l'executable
pyinstaller main.spec

# Créer le répertoire d'installation
sudo mkdir -p /opt/SimpleAutoFramer
sudo cp -r dist/SimpleAutoFramer/* /opt/SimpleAutoFramer/
sudo cp icon.png /opt/SimpleAutoFramer/

# Installer le lanceur automatique
mkdir -p ~/.config/autostart
cp SimpleAutoFramer.desktop ~/.config/autostart/
sed -i "s|/opt/SimpleAutoFramer|$HOME/.local/bin/SimpleAutoFramer|g" ~/.config/autostart/SimpleAutoFramer.desktop

# Charger le module v4l2loopback au démarrage
echo "v4l2loopback" | sudo tee -a /etc/modules-load.d/v4l2loopback.conf

echo "Installation terminée ! Redémarrez pour activer SimpleAutoFramer."