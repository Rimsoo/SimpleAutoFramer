cmake_minimum_required(VERSION 3.10)
project(simpleautoframer)
option(INSTALL_MODE "Enable install mode" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_PREFIX_PATH "/usr")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(OpenCV REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM REQUIRED gtkmm-3.0)
pkg_check_modules(APPINDICATOR REQUIRED ayatana-appindicator3-0.1)

include_directories(${GTKMM_INCLUDE_DIRS} ${APPINDICATOR_INCLUDE_DIRS})

add_executable(simpleautoframer main.cpp MainWindow.cpp ProfileManager.cpp HotkeyListener.cpp)

# Ajouter le sous dossier libv4l2cpp
add_subdirectory(libv4l2cpp EXCLUDE_FROM_ALL)
target_include_directories(simpleautoframer PUBLIC libv4l2cpp/inc)
find_package(PkgConfig REQUIRED)

# Utiliser pkg-config pour localiser X11 et XInput2
pkg_check_modules(X11 REQUIRED x11)
pkg_check_modules(XINPUT2 REQUIRED xi)

# Ajouter les chemins d'inclusion
include_directories(${X11_INCLUDE_DIRS})
include_directories(${XINPUT2_INCLUDE_DIRS})

target_link_libraries(simpleautoframer PRIVATE  ${X11_LIBRARIES} ${XINPUT2_LIBRARIES} ${OpenCV_LIBS} ${GTKMM_LIBRARIES} ${APPINDICATOR_LIBRARIES} libv4l2cpp ${LIBRARIES})

# Copier les fichiers nécessaires dans le dossier de build
file(COPY haarcascade_frontalface_default.xml DESTINATION ${CMAKE_BINARY_DIR})
file(COPY icon.png DESTINATION ${CMAKE_BINARY_DIR})
file(COPY main_window.glade DESTINATION ${CMAKE_BINARY_DIR})
file(COPY simpleautoframer.desktop DESTINATION ${CMAKE_BINARY_DIR})
file(COPY res10_300x300_ssd_iter_140000.caffemodel DESTINATION ${CMAKE_BINARY_DIR})
file(COPY deploy.prototxt DESTINATION ${CMAKE_BINARY_DIR})
file(COPY simpleautoframer-uninstall DESTINATION ${CMAKE_BINARY_DIR})

# --- Installation ---
# Installer l'exécutable dans le répertoire bin
install(TARGETS simpleautoframer
        RUNTIME DESTINATION /usr/bin)

install(FILES simpleautoframer-uninstall
        PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ WORLD_EXECUTE WORLD_READ
        DESTINATION /usr/bin)

# Installer les fichiers de ressources (modèle, icône, interface Glade) dans share/simpleautoframer
install(FILES haarcascade_frontalface_default.xml icon.png main_window.glade res10_300x300_ssd_iter_140000.caffemodel deploy.prototxt
        DESTINATION /usr/share/simpleautoframer)

# Installer le fichier .desktop dans /usr/share/applications
install(FILES simpleautoframer.desktop
        DESTINATION /usr/share/applications)
# Installer le fichier .icon
install(FILES icon.png
        DESTINATION /usr/share/icons/hicolor/64x64/apps)
install(CODE "execute_process(COMMAND \${CMAKE_COMMAND} -E copy
    \"/usr/share/icons/hicolor/64x64/apps/icon.png\"
    \"/usr/share/icons/hicolor/64x64/apps/simpleautoframer.png\")")

# Supprimer icon.png
install(CODE "execute_process(COMMAND \${CMAKE_COMMAND} -E remove
    \"/usr/share/icons/hicolor/64x64/apps/icon.png\")")

configure_file(config.h.in config.h @ONLY)
include_directories(${CMAKE_BINARY_DIR})

if(INSTALL_MODE)
  add_compile_definitions(INSTALL_MODE)
endif()
