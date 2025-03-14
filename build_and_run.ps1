
# Vérifier si le dossier 'build' existe, sinon le créer
if (-Not (Test-Path -Path "build" -PathType Container)) {
    Write-Host "Création du dossier build..."
    New-Item -ItemType Directory -Path "build" | Out-Null
}

# Se déplacer dans le dossier 'build'
Set-Location -Path "build"

# Configurer le projet avec CMake
Write-Host "Configuration du projet avec CMake..."
if (-Not (
cmake .. -G "MSYS Makefiles")) {
    Write-Host "Erreur lors de la configuration CMake"
    cd ..
    exit 1
}

# Compiler le projet
Write-Host "Compilation du projet..."
if (-Not (make)) {
    Write-Host "Erreur lors de la compilation"
    cd ..
    exit 1
}

# Exécuter le programme
Write-Host "Lancement du programme..."
if (-Not (Start-Process -FilePath ".\simpleautoframer.exe" -NoNewWindow -Wait -PassThru)) {
    Write-Host "Erreur lors de l'exécution du programme"
    cd ..
    exit 1
}

# Revenir à la racine du projet
Set-Location -Path ".."
