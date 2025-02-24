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

# Exécuter le programme
echo "Lancement du programme..."
./simpleautoframer || { echo "Erreur lors de l'exécution du programme"; exit 1; }


# Revenir à la racine du projet
cd ..