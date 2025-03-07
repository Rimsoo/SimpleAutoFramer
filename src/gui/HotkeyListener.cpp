// HotkeyListener.cpp
// Inspired by :
// https://github.com/MaartenBaert/ssr/blob/master/src/GUI/HotkeyListener.cpp
#include "HotkeyListener.h"
#include <X11/XKBlib.h>
#include <X11/keysym.h>

#include "gdk/gdkx.h"
HotkeyListener &HotkeyListener::GetInstance() {
  static HotkeyListener instance;
  return instance;
}

HotkeyListener::HotkeyListener()
    : m_display(nullptr), m_xinput_opcode(-1), m_raw_modifiers(0) {}

HotkeyListener::~HotkeyListener() {
  UnregisterAll();
  if (m_display)
    XCloseDisplay(m_display);
}

static GdkFilterReturn xevent_filter(GdkXEvent *gdk_xevent, GdkEvent *event,
                                     gpointer data) {
  XEvent *xevent = (XEvent *)gdk_xevent;
  HotkeyListener *listener = static_cast<HotkeyListener *>(data);

  if (xevent->type == KeyPress || xevent->type == KeyRelease) {
    // Traitez les événements de touche ici
    KeyCode keycode = xevent->xkey.keycode;
    unsigned int modifiers =
        xevent->xkey.state & (ControlMask | ShiftMask | Mod1Mask | Mod4Mask);

    Hotkey hotkey{keycode, modifiers};
    auto it = listener->m_callbacks.find(hotkey);
    if (it != listener->m_callbacks.end()) {
      for (auto &callback : it->second) {
        callback();
      }
    }
  }

  return GDK_FILTER_CONTINUE;
}

void HotkeyListener::Init(Display *display) {
  m_display = display;

  // Ajoutez un filtre pour intercepter les événements X11
  GdkWindow *root_window = gdk_get_default_root_window();
  gdk_window_add_filter(root_window, xevent_filter, this);
}

void HotkeyListener::RegisterHotkey(unsigned int keysym, unsigned int modifiers,
                                    const HotkeyCallback &callback) {
  KeyCode keycode = XKeysymToKeycode(m_display, keysym);
  if (keycode == 0)
    return;

  Hotkey hotkey{keycode, modifiers};
  m_callbacks[hotkey].push_back(callback);
  GrabHotkey(hotkey, true);
}

void HotkeyListener::UnregisterAll() {
  for (auto &pair : m_callbacks) {
    GrabHotkey(pair.first, false);
  }
  m_callbacks.clear();
}

gboolean HotkeyListener::OnXEvent(GIOChannel *source, GIOCondition condition,
                                  gpointer data) {
  HotkeyListener *listener = static_cast<HotkeyListener *>(data);
  XEvent event;

  while (XPending(listener->m_display)) {
    XNextEvent(listener->m_display, &event);
    if (event.xcookie.type == GenericEvent &&
        event.xcookie.extension == listener->m_xinput_opcode) {
      if (XGetEventData(listener->m_display, &event.xcookie)) {
        XIDeviceEvent *xievent = (XIDeviceEvent *)event.xcookie.data;
        listener->ProcessXIEvents(xievent);
        XFreeEventData(listener->m_display, &event.xcookie);
      }
    }
  }
  return TRUE;
}

void HotkeyListener::ProcessXIEvents(XIDeviceEvent *xievent) {
  switch (xievent->evtype) {
  case XI_RawKeyPress: {
    KeySym sym = XkbKeycodeToKeysym(m_display, xievent->detail, 0, 0);
    switch (sym) {
    case XK_Control_L:
    case XK_Control_R:
      m_raw_modifiers |= ControlMask;
      break;
    case XK_Shift_L:
    case XK_Shift_R:
      m_raw_modifiers |= ShiftMask;
      break;
    case XK_Alt_L:
    case XK_Alt_R:
      m_raw_modifiers |= Mod1Mask;
      break;
    case XK_Super_L:
    case XK_Super_R:
      m_raw_modifiers |= Mod4Mask;
      break;
    }

    Hotkey hotkey{xievent->detail, m_raw_modifiers};
    auto it = m_callbacks.find(hotkey);
    if (it != m_callbacks.end()) {
      for (auto &callback : it->second) {
        callback();
      }
    }
    break;
  }
  case XI_RawKeyRelease: {
    KeySym sym = XkbKeycodeToKeysym(m_display, xievent->detail, 0, 0);
    switch (sym) {
    case XK_Control_L:
    case XK_Control_R:
      m_raw_modifiers &= ~ControlMask;
      break;
    case XK_Shift_L:
    case XK_Shift_R:
      m_raw_modifiers &= ~ShiftMask;
      break;
    case XK_Alt_L:
    case XK_Alt_R:
      m_raw_modifiers &= ~Mod1Mask;
      break;
    case XK_Super_L:
    case XK_Super_R:
      m_raw_modifiers &= ~Mod4Mask;
      break;
    }
    break;
  }
  }
}

void HotkeyListener::GrabHotkey(const Hotkey &hotkey, bool grab) {
  unsigned int masks[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};

  for (unsigned int mask : masks) {
    if (grab) {
      XGrabKey(m_display, hotkey.keycode, hotkey.modifiers | mask,
               DefaultRootWindow(m_display), True, GrabModeAsync,
               GrabModeAsync);
    } else {
      XUngrabKey(m_display, hotkey.keycode, hotkey.modifiers | mask,
                 DefaultRootWindow(m_display));
    }
  }
  XFlush(m_display);
}
