// src/ui/shortcuts/SafShortcutAdapter.h
//
// Adapter that plugs the new saf::IShortcutManager (X11 / portal / win32)
// behind the legacy IUiHotkeyListener interface used by MainWindow.
//
// Key change vs. the legacy listener: register-then-start semantics.
// On X11 calls to XGrabKey can be made at any time, but on the Wayland
// portal BindShortcuts is a single-shot session operation. Callers must
// therefore batch RegisterHotkey() calls and then invoke Commit() to
// activate them. MainWindow::setupShortcuts() follows this pattern.
//
// Callbacks fire on the backend's event thread (X11 grab thread or
// D-Bus dispatcher). We marshal them to the GTK main thread via
// g_idle_add so that handlers can safely touch GTK widgets.

#pragma once

#include "IShortcutManager.h"
#include "IUiHotkeyListener.h"

#include <atomic>
#include <memory>
#include <string_view>
#include <vector>

class SafShortcutAdapter : public IUiHotkeyListener {
public:
  SafShortcutAdapter();
  ~SafShortcutAdapter() override;

  void RegisterHotkey(const std::string &accelerator,
                      const HotkeyCallback &callback) override;
  void UnregisterAll() override;

  /// Activates every shortcut registered since the last UnregisterAll() call.
  /// For X11 this performs the XGrabKey calls; for the portal it does the
  /// CreateSession + BindShortcuts round-trip.
  /// Returns the backend name that accepted the bindings, or empty string.
  std::string_view Commit() override;

  /// Debug helper.
  std::string_view backendName() const;

private:
  std::unique_ptr<saf::IShortcutManager> mgr_;
  std::vector<saf::ShortcutDescriptor> pending_;
  std::atomic<int> nextId_{0};
  bool started_{false};
};
