// IUiHotkeyListener.h
#pragma once
#include <functional>
#include <string>
#include <string_view>

using HotkeyCallback = std::function<void()>;

class IUiHotkeyListener {
public:
  virtual ~IUiHotkeyListener() = default;

  /// Register a shortcut. On backends that support immediate grabbing (X11)
  /// this takes effect as soon as Commit() is called. On session-scoped
  /// backends (xdg-desktop-portal) this queues the binding until Commit().
  virtual void RegisterHotkey(const std::string &accelerator,
                              const HotkeyCallback &callback) = 0;

  /// Drop every registered shortcut. Call before batching a new set.
  virtual void UnregisterAll() = 0;

  /// Activate the batch registered since the last UnregisterAll().
  /// Returns the backend name actually used, or an empty string on failure.
  virtual std::string_view Commit() = 0;
};
