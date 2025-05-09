// WaylandHotkeyListener.cpp
#ifdef USE_WAYLAND
#include "WaylandHotkeyListener.h"

WaylandHotkeyListener::WaylandHotkeyListener(GtkApplication *app)
    : m_app(app) {}

void WaylandHotkeyListener::RegisterHotkey(const std::string &accelerator,
                                           const HotkeyCallback &callback) {
  guint accel_id = gtk_application_add_accelerator(
      m_app, accelerator.c_str(), "app.activate_action", nullptr);
  m_accel_ids.push_back(accel_id);
}

void WaylandHotkeyListener::UnregisterAll() {
  for (guint id : m_accel_ids) {
    gtk_application_remove_accelerator(m_app, id);
  }
  m_accel_ids.clear();
}

#endif
