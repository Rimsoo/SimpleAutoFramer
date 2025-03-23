cmake_minimum_required(VERSION 3.25.2)
project(simpleautoframer LANGUAGES CXX CUDA)

option(INSTALL_MODE "Enable install mode" OFF)
option(CMAKE_BUILD_TYPE Debug)

# Utilisation des standards C++20 et CUDA 20 (compatible avec CUDA 12.8)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

set(CMAKE_PREFIX_PATH "/usr")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Collecte des sources depuis src/ ---
file(GLOB PROJECT_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/common/*.cpp"
)

# Détection de l'OS
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(IS_LINUX TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows" OR MSYS)
  set(IS_WINDOWS TRUE)
  set(OpenCV_DIR "/c/msys64/mingw64/lib/cmake/opencv4")
endif()

# --- Recherche des packages externes ---
find_package(OpenCV REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(nlohmann_json 3.11.3 REQUIRED)

if(IS_LINUX)
  pkg_check_modules(GTKMM REQUIRED gtkmm-3.0)
  pkg_check_modules(APPINDICATOR REQUIRED ayatana-appindicator3-0.1)
  # --- Recherche et inclusion de X11 et XInput2 ---
  pkg_check_modules(X11 REQUIRED x11)
  pkg_check_modules(XINPUT2 REQUIRED xi)

  add_subdirectory(libv4l2cpp EXCLUDE_FROM_ALL)

  include_directories(
    ${X11_INCLUDE_DIRS}
    ${XINPUT2_INCLUDE_DIRS}
    ${GTKMM_INCLUDE_DIRS}
    ${APPINDICATOR_INCLUDE_DIRS}
  )

  file(GLOB LINUX_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/gtk/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/camera/v4l2/*.cpp"
  )

  list(APPEND PROJECT_SOURCES ${LINUX_SOURCES})
endif()

add_executable(simpleautoframer ${PROJECT_SOURCES})

if(IS_LINUX)
  target_compile_definitions(simpleautoframer PUBLIC "IS_LINUX")

  target_include_directories(simpleautoframer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/gtk
    ${CMAKE_CURRENT_SOURCE_DIR}/src/camera/v4l2
    ${CMAKE_CURRENT_SOURCE_DIR}/libv4l2cpp/inc
  )

  target_link_libraries(simpleautoframer PRIVATE
    ${X11_LIBRARIES}
    ${XINPUT2_LIBRARIES}
    ${GTKMM_LIBRARIES}
    ${APPINDICATOR_LIBRARIES}
    libv4l2cpp
  )
elseif(IS_WINDOWS)
  target_compile_definitions(simpleautoframer PUBLIC "IS_WINDOWS")
endif()

# Inclusion des headers propres à l'application et de la lib
target_include_directories(simpleautoframer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/camera
)

# Lier OpenCV avec les composants spécifiés (les variables OpenCV_LIBS sont définies par find_package)
target_link_libraries(simpleautoframer PRIVATE
    ${OpenCV_LIBS}
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
if(IS_LINUX)
  install(TARGETS simpleautoframer
        RUNTIME DESTINATION /usr/bin)

  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/data/simpleautoframer-uninstall
        PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE
        GROUP_READ WORLD_EXECUTE WORLD_READ DESTINATION /usr/bin)

  install(FILES
    ${RESOURCE_FILES}
    DESTINATION /usr/share/simpleautoframer
  )

  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/data/simpleautoframer.desktop
        DESTINATION /usr/share/applications)

  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/data/simpleautoframer.png
        DESTINATION /usr/share/icons/hicolor/64x64/apps)
endif()

# --- Configuration du fichier config.h ---
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/config.h.in
        ${CMAKE_BINARY_DIR}/config.h @ONLY)
include_directories(${CMAKE_BINARY_DIR})

# --- Définition de la compilation en mode install si activé ---
if(INSTALL_MODE)
  add_compile_definitions(INSTALL_MODE)
endif()

