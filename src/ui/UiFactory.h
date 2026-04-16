// UiFactory.h
#pragma once
#include "IUi.h"
#include "IUiHotkeyListener.h"

#ifdef IS_LINUX
#include "GtkUi.h"
#include "SafShortcutAdapter.h"
#elif IS_WINDOWS
#include "WindowsUi.h"
#endif

#include <memory>

class UiFactory {
public:
  static IUi *createUi(Core &core) {
#ifdef IS_LINUX
    // The hotkey listener must exist BEFORE GtkUi is constructed, because
    // GtkUi::GtkUi triggers MainWindow::setProfileManager → setupShortcuts,
    // which asks for getHotkeyListener() during that call stack.
    CreateHotkeyListener();
    auto *ui = new GtkUi(core);
    return ui;
#elif IS_WINDOWS
    return new WindowsUi(core);
#endif
  }

  static IUiHotkeyListener *getHotkeyListener() {
    return hotkeyListener_.get();
  }

private:
  static std::unique_ptr<IUiHotkeyListener> hotkeyListener_;

  static void CreateHotkeyListener() {
    if (hotkeyListener_)
      return;
#ifdef IS_LINUX
    // The SAF adapter internally picks the best backend at runtime:
    //   X11 session     -> X11ShortcutManager (XGrabKey)
    //   Wayland session -> PortalShortcutManager (xdg-desktop-portal)
    //   unknown         -> tries portal first (works under XWayland too)
    hotkeyListener_ = std::make_unique<SafShortcutAdapter>();
#endif
  }
};

// Initialisation du membre statique
inline std::unique_ptr<IUiHotkeyListener> UiFactory::hotkeyListener_ = nullptr;
