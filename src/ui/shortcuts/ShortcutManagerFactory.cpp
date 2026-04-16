// src/ui/shortcuts/ShortcutManagerFactory.cpp
//
// Runtime selection of the global shortcut backend.

#include "IShortcutManager.h"

#include <cstdlib>
#include <iostream>
#include <string>

#if defined(SAF_HAS_X11) && SAF_HAS_X11
#  include "X11ShortcutManager.h"
#endif

#if defined(SAF_HAS_PORTAL) && SAF_HAS_PORTAL
#  include "PortalShortcutManager.h"
#endif

namespace saf {

namespace {

std::string sessionType() {
    const char* st = std::getenv("XDG_SESSION_TYPE");
    if (!st) return "";
    return st;
}

bool waylandDisplay() {
    return std::getenv("WAYLAND_DISPLAY") != nullptr;
}

bool x11Display() {
    return std::getenv("DISPLAY") != nullptr;
}

} // namespace

std::unique_ptr<IShortcutManager> createShortcutManager() {
    const std::string session = sessionType();
    const bool hasWayland = waylandDisplay();
    const bool hasX11     = x11Display();

    std::cerr << "[shortcuts] XDG_SESSION_TYPE=\"" << session
              << "\" WAYLAND_DISPLAY=" << (hasWayland ? "yes" : "no")
              << " DISPLAY=" << (hasX11 ? "yes" : "no") << std::endl;

    // Case 1 — pure Wayland session: MUST use the portal. X11 grabs only
    // capture XWayland keys, not global shortcuts.
    if (session == "wayland" || (hasWayland && !hasX11)) {
#if defined(SAF_HAS_PORTAL) && SAF_HAS_PORTAL
        std::cerr << "[shortcuts] Using xdg-desktop-portal backend." << std::endl;
        return std::make_unique<PortalShortcutManager>();
#else
        std::cerr << "[shortcuts] Wayland session detected but portal backend "
                     "was not compiled in. Global shortcuts will not work."
                  << std::endl;
        return nullptr;
#endif
    }

    // Case 2 — X11 session (or XWayland fallback requested).
    if (session == "x11" || (hasX11 && !hasWayland)) {
#if defined(SAF_HAS_X11) && SAF_HAS_X11
        std::cerr << "[shortcuts] Using X11/XInput2 backend." << std::endl;
        return std::make_unique<X11ShortcutManager>();
#endif
    }

    // Case 3 — unknown session. Prefer portal (works under XWayland too
    // since xdg-desktop-portal doesn't care about the display protocol).
#if defined(SAF_HAS_PORTAL) && SAF_HAS_PORTAL
    std::cerr << "[shortcuts] Session type unknown — trying portal." << std::endl;
    return std::make_unique<PortalShortcutManager>();
#elif defined(SAF_HAS_X11) && SAF_HAS_X11
    std::cerr << "[shortcuts] Session type unknown — falling back to X11." << std::endl;
    return std::make_unique<X11ShortcutManager>();
#else
    std::cerr << "[shortcuts] No shortcut backend compiled in." << std::endl;
    return nullptr;
#endif
}

} // namespace saf
