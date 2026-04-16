cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(USE_CUDA              "Enable CUDA acceleration (needs CUDA toolkit)"       OFF)
option(INSTALL_MODE          "Build paths for system install (/usr/share/...)"    OFF)

# Legacy hotkey backends. Superseded by src/ui/shortcuts/*. Kept OFF by
# default; enable only if you need to compile the old listeners for reference.
option(USE_LEGACY_HOTKEYS    "Compile the legacy X11/Wayland hotkey listeners"   OFF)

# Shortcut abstraction (src/ui/shortcuts/*) — picks X11 or xdg-desktop-portal
# at runtime based on XDG_SESSION_TYPE.
option(USE_X11_SHORTCUTS     "Build the X11 global-shortcut backend"              ON)
option(USE_PORTAL_SHORTCUTS  "Build the xdg-desktop-portal (Wayland) backend"     ON)

# Default build type (Release if not specified)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Project declaration (CUDA must be declared *before* project() if used)
# ---------------------------------------------------------------------------
if(USE_CUDA)
    project(simpleautoframer VERSION 0.2.0 LANGUAGES CXX CUDA)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
else()
    project(simpleautoframer VERSION 0.2.0 LANGUAGES CXX)
endif()

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
# Legacy compile-time flags (kept for the legacy listeners; off by default)
# ---------------------------------------------------------------------------
if(USE_LEGACY_HOTKEYS)
    add_definitions(-DUSE_X11 -DUSE_WAYLAND)
endif()

# ---------------------------------------------------------------------------
# External packages — common
# ---------------------------------------------------------------------------
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio objdetect dnn)

# Core.cpp uses cv::cuda::resize / cv::cuda::cvtColor on the DNN path. Until
# Core is refactored to guard those calls behind #ifdef USE_CUDA, we link
# against the OpenCV CUDA modules whenever they are available.
find_package(OpenCV OPTIONAL_COMPONENTS cudaimgproc cudawarping)
if(OpenCV_cudaimgproc_FOUND AND OpenCV_cudawarping_FOUND)
    set(SAF_HAS_OPENCV_CUDA TRUE)
    message(STATUS "OpenCV CUDA modules found (cudaimgproc + cudawarping).")
else()
    set(SAF_HAS_OPENCV_CUDA FALSE)
    message(WARNING
        "OpenCV CUDA modules not found. The DNN detection path in Core.cpp "
        "will fail to link until Core.cpp is refactored to guard cv::cuda "
        "calls behind #ifdef USE_CUDA.")
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
    # Drop the legacy hotkey listeners unless explicitly requested.
    if(NOT USE_LEGACY_HOTKEYS)
        list(FILTER LINUX_SOURCES EXCLUDE REGEX "(X11|Wayland)HotkeyListener\\.cpp$")
    endif()
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
    ${OpenCV_LIBS}
    nlohmann_json::nlohmann_json
    Threads::Threads
)

if(SAF_HAS_OPENCV_CUDA)
    target_compile_definitions(simpleautoframer PRIVATE SAF_HAS_OPENCV_CUDA=1)
    target_link_libraries(simpleautoframer PRIVATE
        opencv_cudaimgproc opencv_cudawarping)
else()
    target_compile_definitions(simpleautoframer PRIVATE SAF_HAS_OPENCV_CUDA=0)
endif()

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
    ${CMAKE_CURRENT_SOURCE_DIR}/data/simpleautoframer.desktop
    ${CMAKE_CURRENT_SOURCE_DIR}/data/res10_300x300_ssd_iter_140000.caffemodel
    ${CMAKE_CURRENT_SOURCE_DIR}/data/deploy.prototxt
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
include(GNUInstallDirs)

if(IS_LINUX)
    install(TARGETS simpleautoframer
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/data/haarcascade_frontalface_default.xml
        ${CMAKE_CURRENT_SOURCE_DIR}/data/simpleautoframer.png
        ${CMAKE_CURRENT_SOURCE_DIR}/data/main_window.glade
        ${CMAKE_CURRENT_SOURCE_DIR}/data/res10_300x300_ssd_iter_140000.caffemodel
        ${CMAKE_CURRENT_SOURCE_DIR}/data/deploy.prototxt
        DESTINATION ${CMAKE_INSTALL_DATADIR}/simpleautoframer)

    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/data/simpleautoframer.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/data/simpleautoframer.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/64x64/apps)

    # Uninstall helper (non-packaged installs only)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/simpleautoframer-uninstall")
        install(PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/data/simpleautoframer-uninstall
            DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()
