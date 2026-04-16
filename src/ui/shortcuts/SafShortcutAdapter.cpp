// src/ui/shortcuts/SafShortcutAdapter.cpp

#include "SafShortcutAdapter.h"
#include "HyprlandBindsSync.h"

#include <glib.h>

#include <iostream>
#include <utility>

namespace {

// Wraps a HotkeyCallback so it runs on the GTK main thread (GLib main loop)
// instead of the shortcut backend's event thread. This prevents crashes if
// the handler touches GTK widgets.
struct IdleDispatch {
  HotkeyCallback cb;
};

gboolean run_on_main_thread(gpointer data) {
  auto *d = static_cast<IdleDispatch *>(data);
  try {
    if (d->cb)
      d->cb();
  } catch (const std::exception &e) {
    std::cerr << "[saf] shortcut handler threw: " << e.what() << std::endl;
  } catch (...) {
    std::cerr << "[saf] shortcut handler threw unknown exception" << std::endl;
  }
  delete d;
  return G_SOURCE_REMOVE;
}

HotkeyCallback makeMainThreadWrapper(HotkeyCallback cb) {
  return [cb = std::move(cb)]() {
    auto *d = new IdleDispatch{cb};
    g_idle_add(run_on_main_thread, d);
  };
}

} // namespace

SafShortcutAdapter::SafShortcutAdapter()
    : mgr_(saf::createShortcutManager()) {}

SafShortcutAdapter::~SafShortcutAdapter() {
  if (mgr_)
    mgr_->stop();
}

void SafShortcutAdapter::RegisterHotkey(const std::string &accelerator,
                                        const HotkeyCallback &callback) {
  if (!mgr_) {
    std::cerr << "[saf] No shortcut backend available; hotkey '" << accelerator
              << "' ignored." << std::endl;
    return;
  }
  if (accelerator.empty())
    return;

  // Human-readable id derived from the accelerator. Makes hyprland.conf
  // entries like `bind = ..., global, simpleautoframer:ctrl_alt_1` readable.
  auto sanitize = [](const std::string &in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
      if (c == '+')
        out.push_back('_');
      else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               (c >= '0' && c <= '9'))
        out.push_back(static_cast<char>(std::tolower(
            static_cast<unsigned char>(c))));
      // other characters (spaces, punctuation) are dropped
    }
    return out;
  };

  saf::ShortcutDescriptor d;
  std::string baseId = sanitize(accelerator);
  if (baseId.empty())
    baseId = "sc";
  // Disambiguate duplicates (same shortcut registered for two profiles).
  std::string candidate = baseId;
  int suffix = 1;
  for (const auto &p : pending_) {
    if (p.id == candidate) {
      candidate = baseId + "_" + std::to_string(++suffix);
    }
  }
  d.id = candidate;
  d.description = "SimpleAutoFramer (" + accelerator + ")";
  d.defaultAccel = accelerator;
  d.callback = makeMainThreadWrapper(callback);
  pending_.push_back(std::move(d));
  nextId_.fetch_add(1);
}

void SafShortcutAdapter::UnregisterAll() {
  if (mgr_)
    mgr_->stop();
  pending_.clear();
  started_ = false;
  nextId_ = 0;
}

std::string_view SafShortcutAdapter::Commit() {
  if (!mgr_)
    return "none";
  if (started_)
    return mgr_->backendName();

  for (const auto &d : pending_) {
    mgr_->registerShortcut(d);
  }

  if (pending_.empty()) {
    // Nothing to bind; keep the manager dormant but consider it "committed"
    // so we don't hit the portal unnecessarily.
    started_ = true;
    return mgr_->backendName();
  }

  if (!mgr_->start()) {
    std::cerr << "[saf] Shortcut backend '" << mgr_->backendName()
              << "' failed to start." << std::endl;
    return {};
  }

  started_ = true;
  // The portal backend binds asynchronously; it will print its own
  // "[portal] Ready." line once the compositor has confirmed.
  std::cerr << "[saf] " << pending_.size() << " shortcut"
            << (pending_.size() > 1 ? "s" : "")
            << " submitted to backend " << mgr_->backendName() << "."
            << std::endl;

  // Convenience for Hyprland users: rewrite the managed block in
  // hyprland.conf so the `bind = ..., global, :<id>` lines match the
  // current shortcut set, then reload. Without this, Hyprland wouldn't
  // route physical keys to the portal until the user edits the config by
  // hand. No-op on other compositors (KDE / GNOME wire bindings themselves).
  if (mgr_->backendName() == "xdg-desktop-portal") {
    syncHyprlandBinds(pending_);
  }

  return mgr_->backendName();
}

std::string_view SafShortcutAdapter::backendName() const {
  return mgr_ ? mgr_->backendName() : std::string_view{"none"};
}
