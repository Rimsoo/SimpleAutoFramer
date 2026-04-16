// src/ui/shortcuts/X11ShortcutManager.cpp
//
// X11 global-shortcut backend. Uses XGrabKey + a background thread to read
// KeyPress events with standard Xlib (sufficient for global grabs — XInput2
// is only needed for per-device dispatching, which this app does not need).
//
// Accelerator syntax accepted:
//   "CTRL+ALT+SHIFT+SUPER+K"
//   "KP_Add", "KP_Subtract", "KP_Multiply", "KP_0".."KP_9"
//   raw keysym name recognised by XStringToKeysym()

#include "X11ShortcutManager.h"

#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <vector>

namespace saf {

namespace {

// The "ignored" modifiers: NumLock (Mod2), CapsLock, ScrollLock.
// To capture a shortcut regardless of their state, we must grab all
// combinations.
constexpr unsigned int kIgnoredMods[] = {
    0,
    Mod2Mask,                           // NumLock
    LockMask,                           // CapsLock
    Mod2Mask | LockMask,
    Mod5Mask,                           // ScrollLock (approx.)
    Mod2Mask | Mod5Mask,
    LockMask | Mod5Mask,
    Mod2Mask | LockMask | Mod5Mask,
};

std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::toupper(c); });
    return s;
}

int errorHandler(Display*, XErrorEvent* ev) {
    char buf[256];
    XGetErrorText(nullptr, ev->error_code, buf, sizeof(buf));
    std::cerr << "[x11] X error: " << buf
              << " (request=" << int(ev->request_code) << ")" << std::endl;
    return 0;
}

} // namespace

X11ShortcutManager::X11ShortcutManager() = default;

X11ShortcutManager::~X11ShortcutManager() { stop(); }

bool X11ShortcutManager::parseAccel(const std::string& accel,
                                    unsigned int& outKeycode,
                                    unsigned int& outMods) const
{
    if (!m_display) return false;

    outMods = 0;
    std::string keyPart;
    std::stringstream ss(accel);
    std::string tok;
    while (std::getline(ss, tok, '+')) {
        const std::string up = toUpper(tok);
        if      (up == "CTRL" || up == "CONTROL") outMods |= ControlMask;
        else if (up == "ALT"  || up == "MOD1")    outMods |= Mod1Mask;
        else if (up == "SHIFT")                   outMods |= ShiftMask;
        else if (up == "SUPER"|| up == "META" || up == "WIN" || up == "MOD4")
                                                  outMods |= Mod4Mask;
        else                                      keyPart = tok;
    }
    if (keyPart.empty()) return false;

    KeySym ks = XStringToKeysym(keyPart.c_str());
    if (ks == NoSymbol) {
        // Try uppercase letter fallback for single-char keys
        if (keyPart.size() == 1) {
            keyPart[0] = std::toupper(static_cast<unsigned char>(keyPart[0]));
            ks = XStringToKeysym(keyPart.c_str());
        }
    }
    if (ks == NoSymbol) {
        std::cerr << "[x11] Unknown keysym: " << keyPart << std::endl;
        return false;
    }
    outKeycode = XKeysymToKeycode(m_display, ks);
    return outKeycode != 0;
}

bool X11ShortcutManager::registerShortcut(ShortcutDescriptor desc) {
    if (desc.id.empty() || desc.defaultAccel.empty()) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    X11ShortcutBinding b{};
    b.desc = std::move(desc);
    m_bindings[b.desc.id] = b;
    // Actual key parsing + grab happens at start() (we need m_display).
    return true;
}

void X11ShortcutManager::unregisterShortcut(const std::string& id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_bindings.erase(id);
    // Re-grab on next start.
}

bool X11ShortcutManager::start() {
    if (m_active.load()) return true;

    XSetErrorHandler(errorHandler);

    m_display = XOpenDisplay(nullptr);
    if (!m_display) {
        std::cerr << "[x11] XOpenDisplay failed — no DISPLAY set?" << std::endl;
        return false;
    }

    // Resolve accelerators now that we have a display
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [id, b] : m_bindings) {
            if (!parseAccel(b.desc.defaultAccel, b.keycode, b.modMask)) {
                std::cerr << "[x11] Failed to parse accelerator for '" << id
                          << "': " << b.desc.defaultAccel << std::endl;
            }
        }
    }

    grabAll();

    m_stopRequested.store(false);
    m_active.store(true);
    m_thread = std::thread(&X11ShortcutManager::eventLoop, this);
    return true;
}

void X11ShortcutManager::stop() {
    if (!m_active.load() && !m_display) return;

    m_stopRequested.store(true);

    // Poke the display to wake up the event loop.
    if (m_display) {
        // XSendEvent to ourselves would be cleaner; for simplicity we just
        // close: XNextEvent will return I/O error, the thread exits.
    }

    if (m_thread.joinable()) m_thread.join();

    if (m_display) {
        ungrabAll();
        XCloseDisplay(m_display);
        m_display = nullptr;
    }
    m_active.store(false);
}

void X11ShortcutManager::grabAll() {
    if (!m_display) return;
    Window root = DefaultRootWindow(m_display);
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [id, b] : m_bindings) {
        if (b.keycode == 0) continue;
        for (unsigned int ignored : kIgnoredMods) {
            XGrabKey(m_display, b.keycode, b.modMask | ignored, root,
                     /*owner_events=*/True, GrabModeAsync, GrabModeAsync);
        }
    }
    XSync(m_display, False);
}

void X11ShortcutManager::ungrabAll() {
    if (!m_display) return;
    Window root = DefaultRootWindow(m_display);
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [id, b] : m_bindings) {
        if (b.keycode == 0) continue;
        for (unsigned int ignored : kIgnoredMods) {
            XUngrabKey(m_display, b.keycode, b.modMask | ignored, root);
        }
    }
    XSync(m_display, False);
}

void X11ShortcutManager::eventLoop() {
    while (!m_stopRequested.load()) {
        // Non-blocking drain
        while (XPending(m_display) > 0) {
            XEvent ev;
            XNextEvent(m_display, &ev);
            if (ev.type != KeyPress) continue;

            const unsigned int kc   = ev.xkey.keycode;
            const unsigned int mods = ev.xkey.state
                & (ControlMask | Mod1Mask | ShiftMask | Mod4Mask);

            std::function<void()> cb;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (auto& [id, b] : m_bindings) {
                    if (b.keycode == kc && b.modMask == mods) {
                        cb = b.desc.callback;
                        break;
                    }
                }
            }
            if (cb) {
                try { cb(); }
                catch (const std::exception& e) {
                    std::cerr << "[x11] handler exception: " << e.what()
                              << std::endl;
                } catch (...) {
                    std::cerr << "[x11] handler: unknown exception" << std::endl;
                }
            }
        }
        // Avoid busy wait
        struct timespec ts{0, 10'000'000}; // 10 ms
        nanosleep(&ts, nullptr);
    }
}

} // namespace saf
