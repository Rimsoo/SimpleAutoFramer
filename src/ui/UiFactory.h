// UiFactory.h
#include "IUi.h"
#ifdef IS_LINUX
#include "GtkUi.h"
#include "IUiHotkeyListener.h"
#elif IS_WINDOWS
#include "WindowsUi.h"
#endif

class UiFactory {
public:
  static IUi *createUi(Core &core) {
#ifdef IS_LINUX
    return new GtkUi(core);
#elif IS_WINDOWS
    return new WindowsUi(core);
#endif
  }

  static std::unique_ptr<IUiHotkeyListener>
  CreateHotkeyListener(GtkApplication *app) {
    const char *session_type = getenv("XDG_SESSION_TYPE");
    if (session_type && strcmp(session_type, "wayland") == 0) {
#ifdef USE_WAYLAND
      return std::make_unique<WaylandHotkeyListener>(app);
#endif
    } else {
#ifdef USE_X11
      return std::make_unique<X11HotkeyListener>();
#endif
    }
    return nullptr;
  }
};
