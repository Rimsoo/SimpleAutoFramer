cmake_minimum_required(VERSION 3.10)
project(simpleautoframer)

option(INSTALL_MODE "Enable install mode" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_PREFIX_PATH "/usr")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Recherche des packages externes ---
find_package(OpenCV REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM REQUIRED gtkmm-3.0)
pkg_check_modules(APPINDICATOR REQUIRED ayatana-appindicator3-0.1)

include_directories(${GTKMM_INCLUDE_DIRS} ${APPINDICATOR_INCLUDE_DIRS})

# --- Sous-répertoire de la bibliothèque interne ---
add_subdirectory(libv4l2cpp EXCLUDE_FROM_ALL)

# --- Collecte des sources depuis src/ ---
file(GLOB_RECURSE PROJECT_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(simpleautoframer ${PROJECT_SOURCES})

# Inclusion des headers propres à l'application et de la lib
target_include_directories(simpleautoframer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/libv4l2cpp/inc
)

# --- Recherche et inclusion de X11 et XInput2 ---
pkg_check_modules(X11 REQUIRED x11)
pkg_check_modules(XINPUT2 REQUIRED xi)

include_directories(${X11_INCLUDE_DIRS} ${XINPUT2_INCLUDE_DIRS})

target_link_libraries(simpleautoframer PRIVATE
    ${X11_LIBRARIES}
    ${XINPUT2_LIBRARIES}
    ${OpenCV_LIBS}
    ${GTKMM_LIBRARIES}
    ${APPINDICATOR_LIBRARIES}
    libv4l2cpp
)

# --- Copie des fichiers ressources dans le dossier de build ---
set(RESOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/data/haarcascade_frontalface_default.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/data/simpleautoframer.png
    ${CMAKE_CURRENT_SOURCE_DIR}/data/main_window.glade
    ${CMAKE_CURRENT_SOURCE_DIR}/data/simpleautoframer.desktop
    ${CMAKE_CURRENT_SOURCE_DIR}/data/res10_300x300_ssd_iter_140000.caffemodel
    ${CMAKE_CURRENT_SOURCE_DIR}/data/deploy.prototxt
    ${CMAKE_CURRENT_SOURCE_DIR}/data/simpleautoframer-uninstall
)

foreach(resource ${RESOURCE_FILES})
  file(COPY ${resource} DESTINATION ${CMAKE_BINARY_DIR})
endforeach()

# --- Installation ---
# Installer l'exécutable dans /usr/bin
install(TARGETS simpleautoframer
        RUNTIME DESTINATION /usr/bin)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/data/simpleautoframer-uninstall
        PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ WORLD_EXECUTE WORLD_READ
        DESTINATION /usr/bin)

# Installer les fichiers ressources dans /usr/share/simpleautoframer
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/data/haarcascade_frontalface_default.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/data/simpleautoframer.png
    ${CMAKE_CURRENT_SOURCE_DIR}/data/main_window.glade
    ${CMAKE_CURRENT_SOURCE_DIR}/data/res10_300x300_ssd_iter_140000.caffemodel
    ${CMAKE_CURRENT_SOURCE_DIR}/data/deploy.prototxt
    DESTINATION /usr/share/simpleautoframer
)

# Installer le fichier .desktop dans /usr/share/applications
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/data/simpleautoframer.desktop
        DESTINATION /usr/share/applications)

# Installer l'icône dans /usr/share/icons/hicolor/64x64/apps et renommer
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/data/simpleautoframer.png
        DESTINATION /usr/share/icons/hicolor/64x64/apps)

# --- Configuration du fichier config.h ---
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/config.h.in ${CMAKE_BINARY_DIR}/config.h @ONLY)
include_directories(${CMAKE_BINARY_DIR})

# --- Définition de la compilation en mode install si activé ---
if(INSTALL_MODE)
  add_compile_definitions(INSTALL_MODE)
endif()

