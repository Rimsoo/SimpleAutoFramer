// UiFactory.h
#pragma once
#include "IUi.h"
#include "IUiHotkeyListener.h"

#ifdef IS_LINUX
#include "GtkUi.h"
#include "gdk/gdkx.h"
#ifdef USE_X11
#include "X11HotkeyListener.h"
#endif
#ifdef USE_WAYLAND
#include "WaylandHotkeyListener.h"
#endif
#elif IS_WINDOWS
#include "WindowsUi.h"
#endif

#include <memory>

class UiFactory {
public:
  static IUi *createUi(Core &core) {
#ifdef IS_LINUX
    auto *ui = new GtkUi(core);
    // Initialiser le listener avec l'application GTK
    CreateHotkeyListener(ui->GetGtkApp());
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

  static void CreateHotkeyListener(GtkApplication *app) {
    if (hotkeyListener_)
      return;

    const char *session_type = getenv("XDG_SESSION_TYPE");
    if (session_type && strcmp(session_type, "wayland") == 0) {
#ifdef USE_WAYLAND
      hotkeyListener_ = std::make_unique<WaylandHotkeyListener>(app);
#endif
    } else {
#ifdef USE_X11
      auto x11hkl = std::make_unique<X11HotkeyListener>();
      x11hkl->Init(gdk_x11_display_get_xdisplay(gdk_display_get_default()));
      hotkeyListener_ = std::move(x11hkl);
#endif
    }
  }
};

// Initialisation du membre statique
inline std::unique_ptr<IUiHotkeyListener> UiFactory::hotkeyListener_ = nullptr;
