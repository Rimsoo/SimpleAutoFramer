cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(INSTALL_MODE          "Build paths for system install (/usr/share/...)"    OFF)
option(INSTALL_UNINSTALLER   "Install the simpleautoframer-uninstall helper (not for distro packages)" ON)

# Shortcut abstraction (src/ui/shortcuts/*) — picks X11 or xdg-desktop-portal
# at runtime based on XDG_SESSION_TYPE.
option(USE_X11_SHORTCUTS     "Build the X11 global-shortcut backend"              ON)
option(USE_PORTAL_SHORTCUTS  "Build the xdg-desktop-portal (Wayland) backend"     ON)

# Default build type (Release if not specified)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

project(simpleautoframer VERSION 0.3.0 LANGUAGES CXX)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Platform detection
# ---------------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(IS_LINUX TRUE)
elseif(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Windows" OR MSYS OR MINGW)
    set(IS_WINDOWS TRUE)
    if(NOT DEFINED OpenCV_DIR)
        # Reasonable default for MSYS2 users; override with -DOpenCV_DIR=...
        set(OpenCV_DIR "C:/msys64/mingw64/lib/cmake/opencv4")
    endif()
endif()

# ---------------------------------------------------------------------------
# External packages — common
# ---------------------------------------------------------------------------
# HINTS beat the PATH-derived prefixes, so a stale /usr/local OpenCV build
# (e.g. one pinned to an old CUDA version) can't shadow the distro package.
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio objdetect dnn
    HINTS /usr/lib/cmake/opencv5 /usr/lib/cmake/opencv4
          /usr/lib/x86_64-linux-gnu/cmake/opencv4
          /usr/lib/aarch64-linux-gnu/cmake/opencv4)
message(STATUS "Using OpenCV ${OpenCV_VERSION} from ${OpenCV_DIR}")
# Each find_package(OpenCV) call below overwrites OpenCV_LIBS, so accumulate.
set(SAF_OPENCV_LIBS ${OpenCV_LIBS})

# OpenCV 5 relocated the classic Haar/LBP cv::CascadeClassifier from the core
# "objdetect" module into the contrib "xobjdetect" module, so it must be linked
# explicitly. On OpenCV 4 the symbol lives in objdetect and no extra module is
# needed.
if(OpenCV_VERSION_MAJOR GREATER_EQUAL 5)
    find_package(OpenCV REQUIRED COMPONENTS xobjdetect)
    list(APPEND SAF_OPENCV_LIBS ${OpenCV_LIBS})
endif()

find_package(PkgConfig REQUIRED)
find_package(nlohmann_json 3.9.0 REQUIRED)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Sources — common (src/*.cpp, src/common/*.cpp)
# ---------------------------------------------------------------------------
file(GLOB PROJECT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/*.cpp"
)

# New shortcut abstraction (always built: its factory is platform-agnostic)
list(APPEND PROJECT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/shortcuts/ShortcutManagerFactory.cpp"
)

# ---------------------------------------------------------------------------
# Linux-specific
# ---------------------------------------------------------------------------
if(IS_LINUX)
    pkg_check_modules(GTKMM        REQUIRED gtkmm-3.0)
    pkg_check_modules(APPINDICATOR REQUIRED ayatana-appindicator3-0.1)
    pkg_check_modules(X11_PKG      REQUIRED x11)
    pkg_check_modules(XINPUT2      REQUIRED xi)

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libv4l2cpp/CMakeLists.txt")
        add_subdirectory(libv4l2cpp EXCLUDE_FROM_ALL)
    else()
        message(FATAL_ERROR
            "libv4l2cpp submodule is missing. "
            "Run: git submodule update --init --recursive")
    endif()

    include_directories(
        ${X11_PKG_INCLUDE_DIRS}
        ${XINPUT2_INCLUDE_DIRS}
        ${GTKMM_INCLUDE_DIRS}
        ${APPINDICATOR_INCLUDE_DIRS}
    )

    file(GLOB LINUX_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/gtk/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/camera/v4l2/*.cpp"
    )
    list(APPEND PROJECT_SOURCES ${LINUX_SOURCES})

    # SAF shortcut adapter — always compiled on Linux.
    list(APPEND PROJECT_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/shortcuts/SafShortcutAdapter.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/shortcuts/HyprlandBindsSync.cpp")

    # --- New shortcut backends (Linux) ---
    if(USE_X11_SHORTCUTS)
        list(APPEND PROJECT_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/shortcuts/X11ShortcutManager.cpp")
    endif()
    if(USE_PORTAL_SHORTCUTS)
        pkg_check_modules(GIO REQUIRED gio-2.0 gio-unix-2.0)
        list(APPEND PROJECT_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/shortcuts/PortalShortcutManager.cpp")
    endif()
endif()

# ---------------------------------------------------------------------------
# Windows-specific
# ---------------------------------------------------------------------------
if(IS_WINDOWS)
    # Until the Windows port is done, only core + Win32 shortcut manager compile.
    list(APPEND PROJECT_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/shortcuts/Win32ShortcutManager.cpp")
    message(STATUS "Windows build selected — only core + Win32 shortcut backend included.")
endif()

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(simpleautoframer ${PROJECT_SOURCES})

target_include_directories(simpleautoframer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/shortcuts
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/camera
    ${CMAKE_BINARY_DIR}   # generated config.h
)

# ---- Common link deps
target_link_libraries(simpleautoframer PRIVATE
    ${SAF_OPENCV_LIBS}
    nlohmann_json::nlohmann_json
    Threads::Threads
)

# ---- Linux link deps
if(IS_LINUX)
    target_compile_definitions(simpleautoframer PUBLIC IS_LINUX SAF_LINUX=1)

    target_include_directories(simpleautoframer PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/gtk
        ${CMAKE_CURRENT_SOURCE_DIR}/src/camera/v4l2
        ${CMAKE_CURRENT_SOURCE_DIR}/libv4l2cpp/inc
    )

    target_link_libraries(simpleautoframer PRIVATE
        ${X11_PKG_LIBRARIES}
        ${XINPUT2_LIBRARIES}
        ${GTKMM_LIBRARIES}
        ${APPINDICATOR_LIBRARIES}
        libv4l2cpp
    )

    if(USE_X11_SHORTCUTS)
        target_compile_definitions(simpleautoframer PRIVATE SAF_HAS_X11=1)
        # X11 libs already linked above
    endif()

    if(USE_PORTAL_SHORTCUTS)
        target_compile_definitions(simpleautoframer PRIVATE SAF_HAS_PORTAL=1)
        target_include_directories(simpleautoframer PRIVATE ${GIO_INCLUDE_DIRS})
        target_link_libraries(simpleautoframer PRIVATE ${GIO_LIBRARIES})
    endif()
endif()

if(IS_WINDOWS)
    target_compile_definitions(simpleautoframer PUBLIC IS_WINDOWS SAF_WINDOWS=1)
endif()

# ---------------------------------------------------------------------------
# Generated config.h
# ---------------------------------------------------------------------------
set(SAF_INSTALL_DATA_DIR "${CMAKE_INSTALL_FULL_DATADIR}/simpleautoframer")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/config.h.in
               ${CMAKE_BINARY_DIR}/config.h @ONLY)
include_directories(${CMAKE_BINARY_DIR})

if(INSTALL_MODE)
    target_compile_definitions(simpleautoframer PRIVATE INSTALL_MODE)
endif()

# ---------------------------------------------------------------------------
# Resources — copy to build dir for dev runs
# ---------------------------------------------------------------------------
set(RESOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/data/haarcascade_frontalface_default.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/data/simpleautoframer.png
    ${CMAKE_CURRENT_SOURCE_DIR}/data/main_window.glade
    ${CMAKE_CURRENT_SOURCE_DIR}/data/main_window.css
    ${CMAKE_CURRENT_SOURCE_DIR}/data/org.simple_auto_framer.desktop
    ${CMAKE_CURRENT_SOURCE_DIR}/data/face_detection_yunet_2023mar.onnx
    ${CMAKE_CURRENT_SOURCE_DIR}/data/simpleautoframer-uninstall
)

# Copy resources at BUILD time (not configure time) so edits to data/*
# propagate without requiring a CMake re-run.
foreach(resource ${RESOURCE_FILES})
    if(EXISTS ${resource})
        get_filename_component(_resource_name ${resource} NAME)
        add_custom_command(
            TARGET simpleautoframer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${resource} ${CMAKE_BINARY_DIR}/${_resource_name}
            VERBATIM)
    else()
        message(WARNING "Missing resource: ${resource}")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Install rules (Linux) — relative paths so DESTDIR works for packaging
# ---------------------------------------------------------------------------
if(IS_LINUX)
    install(TARGETS simpleautoframer
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/data/haarcascade_frontalface_default.xml
        ${CMAKE_CURRENT_SOURCE_DIR}/data/simpleautoframer.png
        ${CMAKE_CURRENT_SOURCE_DIR}/data/main_window.glade
        ${CMAKE_CURRENT_SOURCE_DIR}/data/main_window.css
        ${CMAKE_CURRENT_SOURCE_DIR}/data/face_detection_yunet_2023mar.onnx
        DESTINATION ${CMAKE_INSTALL_DATADIR}/simpleautoframer)

    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/data/org.simple_auto_framer.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/data/simpleautoframer.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/64x64/apps)

    # Uninstall helper (non-packaged installs only)
    if(INSTALL_UNINSTALLER)
        install(PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/data/simpleautoframer-uninstall
            DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()
