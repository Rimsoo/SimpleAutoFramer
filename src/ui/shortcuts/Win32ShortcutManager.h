// src/ui/shortcuts/Win32ShortcutManager.h
//
// Windows global shortcut backend — skeleton for the future port.
// Uses RegisterHotKey() + a dedicated message-only window thread to receive
// WM_HOTKEY messages without tying into any GUI toolkit.

#pragma once

#include "IShortcutManager.h"

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#if defined(_WIN32)

namespace saf {

struct Win32HotkeyBinding {
    int         id;           // RegisterHotKey id (>= 1, <= 0xBFFF)
    unsigned    modifiers;    // MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN
    unsigned    vk;           // Virtual key code (VK_*)
    ShortcutDescriptor desc;
};

class Win32ShortcutManager final : public IShortcutManager {
public:
    Win32ShortcutManager();
    ~Win32ShortcutManager() override;

    std::string_view backendName() const override { return "Win32 RegisterHotKey"; }

    bool registerShortcut(ShortcutDescriptor desc) override;
    void unregisterShortcut(const std::string& id) override;
    bool start() override;
    void stop() override;
    bool isActive() const override { return m_active.load(); }

private:
    void messageLoop();
    bool parseAccel(const std::string& accel,
                    unsigned& outMods, unsigned& outVk) const;

    std::thread m_thread;
    std::atomic<bool> m_active       {false};
    std::atomic<bool> m_stopRequested{false};

    std::mutex m_mutex;
    std::map<std::string, Win32HotkeyBinding> m_bindings;
    int m_nextId {1};

    // Populated inside the worker thread (window belongs to that thread).
    void* m_hwnd {nullptr};         // HWND, opaque here
    unsigned long m_threadId {0};   // DWORD
};

} // namespace saf

#endif // _WIN32
