// src/ui/shortcuts/PortalShortcutManager.h
//
// Wayland global shortcuts via xdg-desktop-portal.
// Spec: org.freedesktop.portal.GlobalShortcuts (since xdg-desktop-portal 1.17)
// https://flatpak.github.io/xdg-desktop-portal/docs/#gdbus-org.freedesktop.portal.GlobalShortcuts
//
// Implementation note — the entire D-Bus conversation runs on a dedicated
// worker thread with its own GMainContext and GMainLoop. The public API
// methods (start/stop/registerShortcut) are non-blocking: they post the
// actual work to that worker thread. This means the GTK main loop is
// never stalled by CreateSession / BindShortcuts round-trips (which can
// take seconds on portals that prompt the user for consent, or forever
// on portal backends that silently drop requests).
//
// Activated callbacks are marshalled back to the GTK main thread via
// g_idle_add so handlers can safely touch widgets.

#pragma once

#include "IShortcutManager.h"

#include <gio/gio.h>
#include <glib.h>

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace saf {

class PortalShortcutManager final : public IShortcutManager {
public:
  PortalShortcutManager();
  ~PortalShortcutManager() override;

  std::string_view backendName() const override {
    return "xdg-desktop-portal";
  }

  bool registerShortcut(ShortcutDescriptor desc) override;
  void unregisterShortcut(const std::string &id) override;
  bool start() override;
  void stop() override;
  bool isActive() const override { return m_active.load(); }

private:
  // Worker-thread entry point.
  void workerMain();

  // Steps scheduled on the worker main loop.
  static gboolean onStartRequested(gpointer user_data);
  static void onBusReady(GObject *source, GAsyncResult *res, gpointer user_data);
  static void onCreateSessionReply(GObject *source, GAsyncResult *res,
                                   gpointer user_data);
  static void onBindShortcutsReply(GObject *source, GAsyncResult *res,
                                   gpointer user_data);
  static void onCreateSessionResponse(GDBusConnection *conn, const char *sender,
                                      const char *path, const char *iface,
                                      const char *signal, GVariant *params,
                                      gpointer user_data);
  static void onBindShortcutsResponse(GDBusConnection *conn, const char *sender,
                                      const char *path, const char *iface,
                                      const char *signal, GVariant *params,
                                      gpointer user_data);
  static void onListShortcutsReply(GObject *source, GAsyncResult *res,
                                   gpointer user_data);
  static void onListShortcutsResponse(GDBusConnection *conn, const char *sender,
                                      const char *path, const char *iface,
                                      const char *signal, GVariant *params,
                                      gpointer user_data);
  static void onActivatedSignal(GDBusConnection *conn, const char *sender,
                                const char *path, const char *iface,
                                const char *signal, GVariant *params,
                                gpointer user_data);

  // Worker-thread-only helpers.
  void bindShortcutsLocked();
  void cleanupOnWorker();
  void fireActivated(const std::string &shortcutId);

  // --- worker-thread state ---
  std::thread m_thread;
  GMainContext *m_ctx{nullptr};
  GMainLoop *m_loop{nullptr};
  GDBusConnection *m_conn{nullptr};
  std::string m_sessionHandle;
  std::string m_sessionToken;
  guint m_activatedSubId{0};
  guint m_createSessionRespSubId{0};
  guint m_bindShortcutsRespSubId{0};
  guint m_listShortcutsRespSubId{0};

  // --- shared state (locked) ---
  std::mutex m_mutex;
  std::map<std::string, ShortcutDescriptor> m_shortcuts;

  std::atomic<bool> m_active{false};
  std::atomic<bool> m_stopRequested{false};
};

} // namespace saf
