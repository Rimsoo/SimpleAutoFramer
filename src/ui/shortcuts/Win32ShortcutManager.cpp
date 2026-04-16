// src/ui/shortcuts/Win32ShortcutManager.cpp
//
// Skeleton implementation. Compiles on Windows only.

#if defined(_WIN32)

#include "Win32ShortcutManager.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

namespace saf {

namespace {

std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::toupper(c); });
    return s;
}

unsigned vkFromString(const std::string& key) {
    if (key.empty()) return 0;
    const std::string up = toUpper(key);

    // Numpad
    if (up == "KP_ADD")      return VK_ADD;
    if (up == "KP_SUBTRACT") return VK_SUBTRACT;
    if (up == "KP_MULTIPLY") return VK_MULTIPLY;
    if (up == "KP_DIVIDE")   return VK_DIVIDE;
    for (int i = 0; i <= 9; ++i) {
        if (up == "KP_" + std::to_string(i)) return VK_NUMPAD0 + i;
    }

    // Function keys
    if (up.size() >= 2 && up[0] == 'F') {
        int n = std::atoi(up.c_str() + 1);
        if (n >= 1 && n <= 24) return VK_F1 + (n - 1);
    }

    // Single char A-Z / 0-9
    if (up.size() == 1) {
        const char c = up[0];
        if (c >= 'A' && c <= 'Z') return static_cast<unsigned>(c);
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c);
    }

    // Common named keys
    if (up == "SPACE")  return VK_SPACE;
    if (up == "TAB")    return VK_TAB;
    if (up == "RETURN" || up == "ENTER") return VK_RETURN;
    if (up == "ESCAPE" || up == "ESC")   return VK_ESCAPE;

    return 0;
}

} // namespace

Win32ShortcutManager::Win32ShortcutManager() = default;
Win32ShortcutManager::~Win32ShortcutManager() { stop(); }

bool Win32ShortcutManager::parseAccel(const std::string& accel,
                                      unsigned& outMods, unsigned& outVk) const {
    outMods = 0;
    outVk   = 0;
    std::stringstream ss(accel);
    std::string tok, keyTok;
    while (std::getline(ss, tok, '+')) {
        const std::string up = toUpper(tok);
        if      (up == "CTRL" || up == "CONTROL") outMods |= MOD_CONTROL;
        else if (up == "ALT")                     outMods |= MOD_ALT;
        else if (up == "SHIFT")                   outMods |= MOD_SHIFT;
        else if (up == "SUPER" || up == "WIN" || up == "META") outMods |= MOD_WIN;
        else                                      keyTok = tok;
    }
    outVk = vkFromString(keyTok);
    if (outVk == 0) {
        std::cerr << "[win32] Unknown key in accel: " << accel << std::endl;
        return false;
    }
    // Avoid auto-repeat flooding
    outMods |= MOD_NOREPEAT;
    return true;
}

bool Win32ShortcutManager::registerShortcut(ShortcutDescriptor desc) {
    if (desc.id.empty()) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    Win32HotkeyBinding b{};
    b.id   = m_nextId++;
    b.desc = std::move(desc);
    if (!parseAccel(b.desc.defaultAccel, b.modifiers, b.vk)) return false;
    m_bindings[b.desc.id] = b;
    return true;
}

void Win32ShortcutManager::unregisterShortcut(const std::string& id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_bindings.find(id);
    if (it == m_bindings.end()) return;
    if (m_active.load() && m_hwnd) {
        PostThreadMessageW(static_cast<DWORD>(m_threadId),
                           WM_APP + 1, it->second.id, 0);
    }
    m_bindings.erase(it);
}

bool Win32ShortcutManager::start() {
    if (m_active.load()) return true;
    m_stopRequested.store(false);
    m_thread = std::thread(&Win32ShortcutManager::messageLoop, this);
    // Wait briefly for the window to come up
    for (int i = 0; i < 100 && !m_active.load(); ++i) {
        Sleep(10);
    }
    return m_active.load();
}

void Win32ShortcutManager::stop() {
    if (!m_active.load()) return;
    m_stopRequested.store(true);
    if (m_threadId) {
        PostThreadMessageW(static_cast<DWORD>(m_threadId), WM_QUIT, 0, 0);
    }
    if (m_thread.joinable()) m_thread.join();
    m_active.store(false);
}

void Win32ShortcutManager::messageLoop() {
    m_threadId = GetCurrentThreadId();

    // Message-only window
    HINSTANCE hinst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = hinst;
    wc.lpszClassName = L"SafHotkeyWindow";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0,0,0,0,
                                HWND_MESSAGE, nullptr, hinst, nullptr);
    if (!hwnd) {
        std::cerr << "[win32] CreateWindowEx failed, GetLastError="
                  << GetLastError() << std::endl;
        return;
    }
    m_hwnd = hwnd;

    // Register all pending bindings
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [id, b] : m_bindings) {
            if (!RegisterHotKey(hwnd, b.id, b.modifiers, b.vk)) {
                std::cerr << "[win32] RegisterHotKey failed for '" << id
                          << "' (error=" << GetLastError() << ")" << std::endl;
            }
        }
    }

    m_active.store(true);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_HOTKEY) {
            const int hkId = static_cast<int>(msg.wParam);
            std::function<void()> cb;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (auto& [id, b] : m_bindings) {
                    if (b.id == hkId) { cb = b.desc.callback; break; }
                }
            }
            if (cb) {
                try { cb(); }
                catch (const std::exception& e) {
                    std::cerr << "[win32] handler: " << e.what() << std::endl;
                }
            }
        } else if (msg.message == WM_APP + 1) {
            // unregister request
            UnregisterHotKey(hwnd, static_cast<int>(msg.wParam));
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (m_stopRequested.load()) break;
    }

    // Cleanup
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [id, b] : m_bindings) UnregisterHotKey(hwnd, b.id);
    }
    DestroyWindow(hwnd);
    UnregisterClassW(L"SafHotkeyWindow", hinst);
    m_hwnd = nullptr;
    m_active.store(false);
}

} // namespace saf

#endif // _WIN32
