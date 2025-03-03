// HotkeyListener.h
// Inspired by :
// https://github.com/MaartenBaert/ssr/blob/master/src/GUI/HotkeyListener.h
#ifndef HOTKEYLISTENER_H
#define HOTKEYLISTENER_H

#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <functional>
#include <glib.h>
#include <map>
#include <set>

typedef std::function<void()> HotkeyCallback;

struct Hotkey {
  int keycode;
  unsigned int modifiers;

  bool operator<(const Hotkey &other) const {
    return std::tie(keycode, modifiers) <
           std::tie(other.keycode, other.modifiers);
  }
};

class HotkeyListener {
public:
  static HotkeyListener &GetInstance();

  void Init(Display *display);
  void RegisterHotkey(unsigned int keysym, unsigned int modifiers,
                      const HotkeyCallback &callback);
  void UnregisterAll();
  std::map<Hotkey, std::vector<HotkeyCallback>> m_callbacks;

private:
  HotkeyListener();
  ~HotkeyListener();

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
