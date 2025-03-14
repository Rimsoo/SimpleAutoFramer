// UiFactory.h
#include "IUi.h"
#ifdef IS_LINUX
#include "GtkUi.h"
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
};
