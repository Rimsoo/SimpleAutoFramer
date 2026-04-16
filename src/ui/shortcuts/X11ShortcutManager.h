// src/ui/shortcuts/X11ShortcutManager.h
//
// X11 global shortcut backend using XGrabKey + XInput2 KeyPress capture.
// This is the factored-out version of the pre-existing Ubuntu code.
//
// Known limitation: under XWayland, XGrabKey only grabs keys pressed inside
// X clients, NOT truly global. For real global shortcuts under Wayland, use
// PortalShortcutManager instead.

#pragma once

#include "IShortcutManager.h"

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>

// Forward-declare Xlib types to avoid pulling <X11/Xlib.h> into the header.
typedef struct _XDisplay Display;

namespace saf {

struct X11ShortcutBinding {
    unsigned int keycode;      // XKeysymToKeycode
    unsigned int modMask;      // ControlMask|Mod1Mask|ShiftMask|Mod4Mask
    ShortcutDescriptor desc;
};

class X11ShortcutManager final : public IShortcutManager {
public:
    X11ShortcutManager();
    ~X11ShortcutManager() override;

    std::string_view backendName() const override { return "X11/XInput2"; }

    bool registerShortcut(ShortcutDescriptor desc) override;
    void unregisterShortcut(const std::string& id) override;
    bool start() override;
    void stop() override;
    bool isActive() const override { return m_active.load(); }

private:
    void eventLoop();
    bool parseAccel(const std::string& accel,
                    unsigned int& outKeycode,
                    unsigned int& outMods) const;
    void grabAll();
    void ungrabAll();

    Display* m_display {nullptr};
    std::thread m_thread;
    std::atomic<bool> m_active {false};
    std::atomic<bool> m_stopRequested {false};

    std::mutex m_mutex;
    std::map<std::string, X11ShortcutBinding> m_bindings;
};

} // namespace saf
