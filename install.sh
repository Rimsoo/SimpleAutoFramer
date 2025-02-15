#!/bin/bash

# Créer le dossier build s'il n'existe pas
if [ ! -d "build" ]; then
    echo "Création du dossier build..."
    mkdir build
fi

# Se déplacer dans le dossier build
cd build

# Configurer le projet avec CMake
echo "Configuration du projet avec CMake..."
cmake .. || { echo "Erreur lors de la configuration CMake"; exit 1; }

# Compiler le projet
echo "Compilation du projet..."
make || { echo "Erreur lors de la compilation"; exit 1; }

# Verifier le sudo
if [ "$EUID" -ne 0 ]; then
    cd ..
    echo "Ce script nécessite les droits d'administrateur pour installer le programme."
    echo "Veuillez lancer: cd build && sudo make install"
    echo "Ou bien: sudo ./install.sh"
    exit 1
fi

# Installer le projet (nécessite les droits d'écriture sur /usr/local par défaut)
echo "Installation du projet..."
sudo make install || { echo "Erreur lors de l'installation"; exit 1; }

# Revenir à la racine du projet
cd ..

# Lancer l'exécutable installé
echo "Lancement du programme..."
/usr/local/bin/simpleautoframer || { echo "Erreur lors de l'exécution du programme"; exit 1; }

