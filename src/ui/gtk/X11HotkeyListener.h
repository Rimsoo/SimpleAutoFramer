// HotkeyListener.h
// Inspired by :
// https://github.com/MaartenBaert/ssr/blob/master/src/GUI/HotkeyListener.h
// X11HotkeyListener.h
#ifdef USE_X11
#ifndef HOTKEYLISTENER_H
#define HOTKEYLISTENER_H

#include "IUiHotkeyListener.h"
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <boost/algorithm/string.hpp>
#include <functional>
#include <glib.h>
#include <map>
#include <set>
#include <sstream>
#include <string>

typedef std::function<void()> HotkeyCallback;

struct Hotkey {
  int keycode;
  unsigned int modifiers;

  bool operator<(const Hotkey &other) const {
    return std::tie(keycode, modifiers) <
           std::tie(other.keycode, other.modifiers);
  }
};

class X11HotkeyListener : public IUiHotkeyListener {
public:
  X11HotkeyListener();
  ~X11HotkeyListener();
  static X11HotkeyListener &GetInstance();

  void Init(Display *display);
  void RegisterHotkey(const std::string &accelerator,
                      const HotkeyCallback &callback) override;
  void UnregisterAll() override;
  std::map<Hotkey, std::vector<HotkeyCallback>> m_callbacks;

private:
  void ProcessXIEvents(XIDeviceEvent *xievent);
  void GrabHotkey(const Hotkey &hotkey, bool grab);
  static gboolean OnXEvent(GIOChannel *source, GIOCondition condition,
                           gpointer data);

  Display *m_display;
  int m_xinput_opcode;
  std::set<int> m_master_keyboards;
  unsigned int m_raw_modifiers;
  guint m_io_watch;
};

#endif // HOTKEYLISTENER_H
#endif
