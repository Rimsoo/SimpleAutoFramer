// WaylandHotkeyListener.h
#ifdef USE_WAYLAND
#ifndef HOTKEYLISTENER_H
#define HOTKEYLISTENER_H

#include "IUiHotkeyListener.h"
#include <gtk/gtk.h>

class WaylandHotkeyListener : public IUiHotkeyListener {
public:
  WaylandHotkeyListener(GtkApplication *app);
  void RegisterHotkey(const std::string &accelerator,
                      const HotkeyCallback &callback) override;
  void UnregisterAll() override;

private:
  GtkApplication *m_app;
  std::vector<guint> m_accel_ids;
};

#endif // HOTKEYLISTENER_H
#endif
