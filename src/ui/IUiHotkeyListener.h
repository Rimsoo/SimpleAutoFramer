// IUiHotkeyListener.h
#pragma once
#include <functional>
#include <string>

using HotkeyCallback = std::function<void()>;

class IUiHotkeyListener {
public:
  virtual ~IUiHotkeyListener() = default;
  virtual void RegisterHotkey(const std::string &accelerator,
                              const HotkeyCallback &callback) = 0;
  virtual void UnregisterAll() = 0;
};
