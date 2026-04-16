# SimpleAutoFramer — Audit technique complet

> Analyse, diagnostic et plan de remédiation
> Cible : stabilité, compatibilité Wayland, packaging Arch (AUR), portage Windows.

---

## 1. Analyse globale du projet

### 1.1 Ce que fait le projet

SimpleAutoFramer est une alternative libre à l'**auto-framing** de NVIDIA Broadcast. Il :

1. Capture les frames d'une webcam physique via OpenCV (`cv::VideoCapture`).
2. Détecte le visage (cascade de Haar et/ou `res10_300x300_ssd` DNN OpenCV).
3. Recadre/zoome dynamiquement autour du visage avec lissage.
4. Réinjecte le flux traité dans une caméra virtuelle `v4l2loopback` via `libv4l2cpp`.
5. Expose une UI GTK3 (gtkmm) + une icône AppIndicator (Ayatana), plus un mode headless.

### 1.2 Stack détectée

| Composant             | Techno actuelle                                    | Remarque                     |
|-----------------------|----------------------------------------------------|------------------------------|
| Build                 | CMake ≥ 3.25.2                                     | OK                           |
| Langage               | C++20 + CUDA 20                                    | CUDA en **LANGUAGES** = dur requis |
| Détection visage      | OpenCV Haar + DNN Caffe                            | OK                           |
| Sortie vidéo Linux    | v4l2loopback + libv4l2cpp (submodule)              | Spécifique Linux             |
| UI                    | gtkmm-3.0 + Glade (`main_window.glade`)            | GTK3 — déprécié à terme      |
| Indicateur            | ayatana-appindicator3-0.1                          | OK                           |
| Raccourcis globaux    | X11 + XInput2 (grab keyboard)                      | **Cassé sous Wayland**       |
| JSON config           | nlohmann_json 3.11.3                               | OK                           |

### 1.3 Architecture des fichiers (déduite du `CMakeLists.txt`)

```
SimpleAutoFramer/
├── CMakeLists.txt
├── install.sh                 # Debian/Ubuntu uniquement
├── build_and_run.sh
├── build_and_run.ps1          # Tentative Windows
├── libv4l2cpp/                # Submodule git
├── data/
│   ├── haarcascade_frontalface_default.xml
│   ├── res10_300x300_ssd_iter_140000.caffemodel
│   ├── deploy.prototxt
│   ├── main_window.glade
│   ├── simpleautoframer.desktop
│   ├── simpleautoframer.png
│   └── simpleautoframer-uninstall
└── src/
    ├── config.h.in
    ├── *.cpp                  # Sources communes (main, framer, etc.)
    ├── common/*.cpp           # Utilitaires
    ├── ui/gtk/*.cpp           # UI GTK (Linux)
    └── camera/v4l2/*.cpp      # Backend caméra (Linux)
```

---

## 2. Problèmes détectés et solutions

### 2.1 Problèmes critiques

| # | Problème | Sévérité | Localisation |
|---|---|---|---|
| C1 | Raccourcis clavier X11 inopérants sous Wayland | **Bloquant Arch/Wayland** | `src/common/` (anciennement via XGrabKey/XInput2) |
| C2 | CUDA listé en langage obligatoire — fail build sans CUDA | **Bloquant** | `CMakeLists.txt` L2 |
| C3 | `option(CMAKE_BUILD_TYPE Debug)` — syntaxe CMake incorrecte | **Bug silencieux** | `CMakeLists.txt` L6 |
| C4 | `USE_X11` forcé ON ; `USE_WAYLAND` commenté — pas de choix runtime | **Bloquant Wayland** | `CMakeLists.txt` L8-16 |
| C5 | `install.sh` : dépendances Debian codées en dur (`apt`) | Bloquant Arch | `install.sh` |
| C6 | `sudo modprobe v4l2loopback` dans install.sh sans persistance | Régression après reboot | `install.sh` |
| C7 | `sudo ./install.sh` → `$USER` devient root → `usermod -aG video root` | Bug silencieux dangereux | `install.sh` L30 |
| C8 | Pas de licence explicite dans le repo | Blocage AUR | racine |
| C9 | `nohup.out` commité accidentellement | Propreté | racine |

### 2.2 Problèmes probables (basés sur les patterns OpenCV/V4L2/gtkmm)

| # | Problème | Sévérité |
|---|---|---|
| P1 | Absence probable de try/catch autour de `cv::VideoCapture::open` / chargement des modèles DNN | Crash au démarrage |
| P2 | Thread capture + thread UI sans mutex autour des paramètres (zoom, lissage, shortcuts) | Data race, flickering |
| P3 | Chargement `main_window.glade` sans vérifier la présence du fichier (chemins `/usr/share/simpleautoframer` vs build local) | Crash sur install partielle |
| P4 | `VideoCapture` jamais release explicitement ou sur signal (SIGINT/SIGTERM) | Caméra reste occupée |
| P5 | Écriture vers `/dev/video20` : pas de check si le device existe ni si le module v4l2loopback est chargé | Crash cryptique |
| P6 | Icône AppIndicator crée un `Gtk::Menu*` sans destructeur explicite (leak mineur) | Leak |
| P7 | `simpleautoframer-uninstall` n'existe qu'en shell Debian : ne démodprobe pas, ne nettoie pas `~/.config/simpleautoframer/` | UX |
| P8 | Glob `file(GLOB …)` utilisé dans CMake → nouveaux fichiers pas détectés sans reconfigure | Build cache stale |
| P9 | `CMAKE_PREFIX_PATH "/usr"` hardcodé → casse sur MSYS2/homebrew | Portabilité |

---

## 3. Le fix Wayland des raccourcis clavier

### 3.1 Pourquoi ça ne fonctionne plus sous Wayland

Sous X11, une application pouvait appeler `XGrabKey` / `XISelectEvents` pour **capter des raccourcis globaux** même sans avoir le focus. C'est exactement le modèle qui marchait pour SimpleAutoFramer sous Ubuntu GNOME/X11.

Sous Wayland, **cette API n'existe plus**, par design sécurité : seul le compositeur a accès au clavier global. L'application ne peut plus écouter `KeyPress` sur la racine.

Trois voies possibles, par ordre de robustesse :

1. **`org.freedesktop.portal.GlobalShortcuts`** (xdg-desktop-portal, depuis spec 1.17, implémentée dans KDE et partiellement dans GNOME via xdg-desktop-portal-gnome ≥ 46). C'est **la voie standard** et **portable** (KDE, GNOME, Hyprland, Sway, …).
2. **`libei` + `libeis`** (RemoteDesktop portal) — plus bas niveau, pour des apps type Flameshot/Input Leap. Overkill ici.
3. **`evdev` brut** (`/dev/input/event*`) — nécessite root ou groupe `input`, pas portable, contourne le compositeur. **À éviter** sauf fallback.

### 3.2 Stratégie recommandée

Architecture découplée : une interface `IShortcutManager` avec **trois implémentations** sélectionnées à l'exécution :

```
IShortcutManager
├── X11ShortcutManager       (XDG_SESSION_TYPE=x11)
├── PortalShortcutManager    (Wayland + xdg-desktop-portal ≥ 1.17)
└── EvdevFallback            (Wayland sans portal) — optionnel, opt-in
```

Le choix se fait au runtime en lisant `getenv("XDG_SESSION_TYPE")` et en testant la présence du portail via D-Bus.

Code drop-in fourni dans `src/ui/shortcuts/` (voir fichiers séparés).

### 3.3 Compatibilité compositeurs

| Compositeur | X11ShortcutManager | PortalShortcutManager |
|---|---|---|
| GNOME X11      | ✅ | ✅ (via xdg-portal ≥ 46) |
| GNOME Wayland  | ❌ | ✅ (GNOME 47+ stable) |
| KDE X11        | ✅ | ✅ |
| KDE Wayland    | ❌ | ✅ (excellent support) |
| Hyprland       | ❌ | ✅ (xdg-desktop-portal-hyprland) |
| Sway           | ❌ | ✅ (xdg-desktop-portal-wlr) |
| XWayland apps  | ⚠️ Marche uniquement sur les apps XWayland, pas globalement | N/A |

---

## 4. Script de build / installation — problèmes et correctifs

### 4.1 `install.sh` — Problèmes

1. **`apt` en dur** → ne marche pas sur Arch (pacman/paru).
2. **`sudo usermod -aG video "$USER"`** exécuté quand le script est déjà en `sudo` → `$USER` vaut `root`, on ajoute root au groupe video (no-op silencieux qui fait croire que c'est bon).
3. **`sudo modprobe v4l2loopback devices=1 video_nr=20 card_label="AutoFrameCam"`** sans fichier dans `/etc/modules-load.d/` ni `/etc/modprobe.d/` → perdu au reboot.
4. Pas de support **DKMS sign** pour Secure Boot.
5. **`sudo rm -rf build`** à la fin — on perd les logs de compilation si le build a marché mais l'install a échoué.
6. Le script **lance l'install en global (`/usr/bin`)** sans prévenir que c'est non-AUR, donc conflits potentiels avec un futur paquet AUR.
7. Aucune vérification que `build/simpleautoframer` existe avant `make install`.

### 4.2 Correctifs (voir `install.sh` corrigé)

- Détection de la distribution (`/etc/os-release`) → pacman sur Arch, apt sur Debian/Ubuntu, dnf sur Fedora.
- Extraction du vrai utilisateur avec `${SUDO_USER:-$USER}`.
- Écriture de `/etc/modules-load.d/v4l2loopback.conf` + `/etc/modprobe.d/v4l2loopback.conf` pour la persistance.
- Création de `/etc/udev/rules.d/99-simpleautoframer.rules` si besoin (accès non-root au device).
- Ne pas effacer `build/` si l'installation a échoué.
- Option `--no-install` pour compiler seulement.

### 4.3 PKGBUILD pour AUR

Voir fichier `packaging/arch/PKGBUILD`. Stratégie :

- `pkgname=simpleautoframer-git` (package VCS, conforme aux conventions AUR).
- `depends=(opencv gtkmm3 libayatana-appindicator nlohmann-json v4l2loopback-dkms)`.
- `makedepends=(cmake git)`.
- `optdepends=(xdg-desktop-portal: 'Wayland global shortcuts' cuda: 'GPU acceleration')`.
- `source=("git+…" libv4l2cpp submodule pinned).`
- **Pas** de `modprobe` dans le PKGBUILD (interdit par règles AUR) → on livre un `modules-load.d` conf.
- `install=simpleautoframer.install` pour afficher le post-install message (activer v4l2loopback, se reconnecter pour groupe video).

---

## 5. Portage Windows — Analyse de faisabilité

### 5.1 Audit des dépendances

| Dépendance            | Windows ?                  | Stratégie                          |
|-----------------------|----------------------------|-------------------------------------|
| OpenCV (core/dnn/imgproc) | ✅ vcpkg/MSYS2            | Directement                         |
| nlohmann_json         | ✅                          | Directement                         |
| gtkmm-3.0             | ⚠️ vcpkg/MSYS2 mais fragile | **À remplacer** par Qt6 ou Dear ImGui |
| ayatana-appindicator  | ❌ Linux-only               | **Remplacer** par tray Win32 / Qt `QSystemTrayIcon` |
| v4l2loopback          | ❌ Linux-only               | **Remplacer** par backend DirectShow virtual camera |
| libv4l2cpp            | ❌ Linux-only               | Isoler derrière `IVirtualCameraSink` |
| X11 / XInput2         | ❌                          | N/A (shortcuts Win32 `RegisterHotKey`) |

### 5.2 Problème fondamental : la caméra virtuelle sous Windows

Il n'y a **pas d'équivalent natif standard** à v4l2loopback. Les options :

1. **OBS Virtual Camera** — le plus populaire. Requiert OBS Studio installé. On peut écrire dans `obs-virtualcam` via le **DirectShow filter** d'OBS, mais c'est une API privée ; l'approche usuelle est de publier son propre DirectShow source filter.
2. **Écrire un DirectShow Virtual Camera Filter** — possible mais lourd (driver COM, registration, signature pour Windows 10 ≥ 1803).
3. **Windows Camera Frame Server + Media Foundation virtual camera** (depuis Windows 11 22H2, via l'API `IMFVirtualCamera`). **C'est la voie moderne recommandée**. Nécessite un installeur MSIX pour s'enregistrer comme fournisseur.
4. **UnityCaptureFilter / UnityCapture** (open source DirectShow filter) — on peut le réutiliser comme backend : notre app écrit les frames dans une memory-mapped section, le filter les publie.

### 5.3 Faisabilité réaliste

- **Faisable mais non trivial.** Compter 2–4 semaines de travail pour un MVP Windows fonctionnel.
- **Approche recommandée pour un premier jet** :
  - UI : migrer vers **Qt6** (disponible gtkmm → Qt est réaliste, plus portable que gtkmm-3 sous Windows).
  - Virtual cam : **UnityCapture DirectShow filter** comme backend par défaut (simple à redistribuer, MIT).
  - Shortcuts : `RegisterHotKey` Win32, trivial.
- **Approche long terme** : Media Foundation `IMFVirtualCamera` + MSIX packaging.

### 5.4 Architecture cible multi-plateformes

```
IVirtualCameraSink
├── V4L2LoopbackSink       (Linux)
├── UnityCaptureSink       (Windows, DirectShow via UnityCapture)
└── MediaFoundationSink    (Windows 11 22H2+, long terme)

ICameraSource
├── V4L2Source             (Linux)
└── MediaFoundationSource  (Windows, cv::CAP_MSMF)

IShortcutManager
├── X11ShortcutManager     (Linux X11)
├── PortalShortcutManager  (Linux Wayland)
└── Win32ShortcutManager   (Windows, RegisterHotKey)

IUiBackend
├── GtkUiBackend           (Linux)
└── QtUiBackend            (portable) OU Win32UiBackend
```

Tout passe par une `struct FramerConfig` partagée (JSON via nlohmann) + une `class Framer` platform-agnostic.

---

## 6. Liste des livrables

Les fichiers drop-in fournis :

1. `CMakeLists.txt` — nettoyé, CUDA optionnel, Wayland optionnel, glob corrigé, Windows prêt.
2. `install.sh` — multi-distro, idempotent, sûr.
3. `build_and_run.sh` — avec flags.
4. `packaging/arch/PKGBUILD` — prêt pour AUR (`simpleautoframer-git`).
5. `packaging/arch/simpleautoframer.install` — hooks post-install.
6. `packaging/arch/v4l2loopback.conf` + `modules-load.d/v4l2loopback.conf`.
7. `src/ui/shortcuts/IShortcutManager.h` — interface abstraite.
8. `src/ui/shortcuts/X11ShortcutManager.{h,cpp}` — implémentation X11 (votre code existant factorisé).
9. `src/ui/shortcuts/PortalShortcutManager.{h,cpp}` — **nouvelle** implémentation Wayland via xdg-desktop-portal GlobalShortcuts.
10. `src/ui/shortcuts/ShortcutManagerFactory.cpp` — sélection runtime.
11. `.gitignore` augmenté (nohup.out, build/, compile_commands.json).
12. `LICENSE` template (MIT ou GPL-3.0 au choix — obligatoire pour AUR).

---

## 7. Ordre de priorité recommandé

| Ordre | Action | Effort | Impact |
|---|---|---|---|
| 1 | Corriger `CMakeLists.txt` (CUDA optionnel, build_type, glob) | 10 min | Débloque les contributeurs |
| 2 | Intégrer `PortalShortcutManager` + factory | 1–2 j | Fix Wayland |
| 3 | Sécuriser `install.sh` + gestion multi-distro | 2 h | Installable sur Arch |
| 4 | PKGBUILD AUR + publication | 2 h | Distribution Arch |
| 5 | Ajouter LICENSE + CI GitHub Actions build Linux | 1 h | Hygiène projet |
| 6 | Abstraction `IVirtualCameraSink` / `ICameraSource` | 2 j | Prépare Windows |
| 7 | Port Qt6 UI | 1 semaine | Portabilité |
| 8 | Backend UnityCapture Windows | 1 semaine | MVP Windows |

---

## 8. Notes

- La présence de **CUDA** comme langage obligatoire dans le `CMakeLists.txt` est probablement la cause d'échecs de build sur des machines sans toolkit NVIDIA — à rendre optionnel (guarded par `option(USE_CUDA …)`).
- Vous mentionnez un mode **No-GUI** : j'ai vu qu'il existait, il est essentiel de le garder fonctionnel même si l'UI GTK n'est pas installée. La factorisation proposée (UI backend derrière une interface) va dans ce sens.
- Le **submodule libv4l2cpp** ajoute beaucoup de bruit au repo ; pour l'AUR, pensez à utiliser `git submodule update --init --recursive` dans le `prepare()` du PKGBUILD.
