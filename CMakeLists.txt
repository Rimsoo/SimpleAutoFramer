cmake_minimum_required(VERSION 3.10)
project(simpleautoframer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_PREFIX_PATH "/usr")

find_package(OpenCV REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM REQUIRED gtkmm-3.0)
pkg_check_modules(APPINDICATOR REQUIRED ayatana-appindicator3-0.1)

include_directories(${GTKMM_INCLUDE_DIRS} ${APPINDICATOR_INCLUDE_DIRS})

add_executable(simpleautoframer main.cpp MainWindow.cpp)

# Ajouter le sous dossier libv4l2cpp 
add_subdirectory(libv4l2cpp EXCLUDE_FROM_ALL)
target_include_directories(simpleautoframer PUBLIC libv4l2cpp/inc)

target_link_libraries(simpleautoframer PRIVATE ${OpenCV_LIBS} ${GTKMM_LIBRARIES} ${APPINDICATOR_LIBRARIES} libv4l2cpp ${LIBRARIES})

# Copier les fichiers nécessaires dans le dossier de build
file(COPY haarcascade_frontalface_default.xml DESTINATION ${CMAKE_BINARY_DIR})
file(COPY icon.png DESTINATION ${CMAKE_BINARY_DIR})
file(COPY main_window.glade DESTINATION ${CMAKE_BINARY_DIR})
file(COPY simpleautoframer.Desktop DESTINATION ${CMAKE_BINARY_DIR})

# --- Installation ---
# Installer l'exécutable dans le répertoire bin
install(TARGETS simpleautoframer
        RUNTIME DESTINATION bin)

# Installer les fichiers de ressources (modèle, icône, interface Glade) dans share/simpleautoframer
install(FILES haarcascade_frontalface_default.xml icon.png main_window.glade
        DESTINATION share/simpleautoframer)

# Installer le fichier .desktop dans /usr/share/applications
# install(FILES simpleautoframer.Desktop
#         DESTINATION /usr/share/applications)
# Installer le fichier .icon dans /usr/share/icons/simpleautoframer/simpleautoframer.png
# install(FILES icon.png simpleautoframer.Desktop
#         DESTINATION /usr/share/simpleautoframer/simpleautoframer.png)

configure_file(config.h.in config.h @ONLY)
include_directories(${CMAKE_BINARY_DIR})

option(INSTALL_MODE "Enable install mode" OFF)

if(INSTALL_MODE)
    add_compile_definitions(INSTALL_MODE)
endif()
