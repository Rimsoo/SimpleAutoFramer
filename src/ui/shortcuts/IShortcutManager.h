// src/ui/shortcuts/IShortcutManager.h
//
// Abstract interface for global keyboard shortcuts. Implementations:
//  - X11ShortcutManager      (X11/XWayland with XInput2 + XGrabKey)
//  - PortalShortcutManager   (Wayland via xdg-desktop-portal GlobalShortcuts)
//  - Win32ShortcutManager    (Windows, RegisterHotKey) — TODO
//
// Design: callers register a "shortcut id" + default accelerator string
// (KDE-style, e.g. "CTRL+ALT+SHIFT+SUPER+K"). The manager resolves it to the
// backend's native representation and invokes the callback when triggered.
//
// Thread safety: callbacks are invoked from the backend's event thread.
// They MUST dispatch heavy work to another thread or to the UI thread
// (e.g. Glib::signal_idle() for GTK).

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace saf {

struct ShortcutDescriptor {
    std::string id;               // Stable internal id, e.g. "toggle_framing"
    std::string description;      // Human-readable, shown in portal UI
    std::string defaultAccel;     // e.g. "CTRL+ALT+K", portal-style
    std::function<void()> callback;
};

class IShortcutManager {
public:
    virtual ~IShortcutManager() = default;

    /// Name of the backend (for logs / UI).
    virtual std::string_view backendName() const = 0;

    /// Register a shortcut. May fail silently if the backend has a permanent
    /// registration model (e.g. the portal) — in which case registration is
    /// deferred to `start()`. Returns true on success.
    virtual bool registerShortcut(ShortcutDescriptor desc) = 0;

    /// Unregister by id. No-op if not registered.
    virtual void unregisterShortcut(const std::string& id) = 0;

    /// Start the event loop / bind. After this call, callbacks start firing.
    /// On the portal backend this asks the user to confirm the shortcuts,
    /// which requires the application to be running inside a D-Bus session.
    virtual bool start() = 0;

    /// Stop listening. Callbacks won't fire anymore.
    virtual void stop() = 0;

    /// Returns true if `start()` succeeded and the backend is currently live.
    virtual bool isActive() const = 0;
};

/// Create the best available shortcut manager for the current session.
/// Order of preference at runtime:
///   1. XDG_SESSION_TYPE=x11 and X11 backend available → X11ShortcutManager
///   2. XDG_SESSION_TYPE=wayland and portal available  → PortalShortcutManager
///   3. If XDG_SESSION_TYPE is unset: try X11 then portal.
///   4. Otherwise → null (caller should log and continue without shortcuts).
std::unique_ptr<IShortcutManager> createShortcutManager();

} // namespace saf
