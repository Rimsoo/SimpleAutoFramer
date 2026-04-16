// src/ui/shortcuts/PortalShortcutManager.cpp
//
// Async implementation of the Wayland global-shortcut backend using
// org.freedesktop.portal.GlobalShortcuts.
//
// Architecture:
//   start() spawns a worker thread. The worker owns its own GMainContext
//   and GMainLoop; it performs ALL D-Bus work asynchronously. The GTK
//   main thread is never blocked. Shortcut-activated callbacks are
//   dispatched back to the GTK main thread via g_idle_add so the
//   application can touch widgets safely.
//
// Flow on the worker thread:
//   1. g_bus_get (async)                  -> onBusReady
//   2. CreateSession call (async)         -> onCreateSessionReply
//      The reply gives a request object path; we also subscribe to the
//      Request::Response signal on that path and wait for it in
//      onCreateSessionResponse to extract the session_handle.
//   3. BindShortcuts call (async)         -> onBindShortcutsReply
//      Same pattern. Response confirms the bindings are active.
//   4. Subscribe to GlobalShortcuts::Activated for the lifetime of the
//      session.
//
// The portal spec places a per-call timeout on BindShortcuts because KDE
// prompts the user for consent; GNOME 47+ binds immediately; Hyprland's
// portal may need to show the binding to the user. We don't impose a
// short timeout ourselves — the portal will either emit Response or the
// user will cancel.

#include "PortalShortcutManager.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <random>
#include <sstream>
#include <utility>
#include <vector>

namespace saf {

namespace {

constexpr const char *PORTAL_BUS = "org.freedesktop.portal.Desktop";
constexpr const char *PORTAL_PATH = "/org/freedesktop/portal/desktop";
constexpr const char *GS_IFACE = "org.freedesktop.portal.GlobalShortcuts";
constexpr const char *REQ_IFACE = "org.freedesktop.portal.Request";

// Per-call soft timeout in ms. Long enough for a user consent prompt.
constexpr int PORTAL_CALL_TIMEOUT_MS = 30'000;

std::string randomToken(const char *prefix) {
  static std::mt19937_64 rng{std::random_device{}()};
  std::ostringstream oss;
  oss << prefix << "_" << std::hex << rng();
  return oss.str();
}

// Compute the full request handle path from the unique bus name + token.
// xdg-desktop-portal echoes this handle back on the Response signal.
std::string computeHandlePath(GDBusConnection *conn, const std::string &token) {
  const char *unique = g_dbus_connection_get_unique_name(conn);
  if (!unique)
    return {};
  std::string sender = unique[0] == ':' ? unique + 1 : unique;
  std::replace(sender.begin(), sender.end(), '.', '_');
  return std::string{"/org/freedesktop/portal/desktop/request/"} + sender +
         "/" + token;
}

// Normalize a legacy SimpleAutoFramer accelerator string ("Ctrl+Alt+K",
// "KP_Add") into the portal/GNOME "<Control><Alt>k" format.
std::string normalizeAccel(const std::string &in) {
  std::string cur, key, mods;
  auto flush = [&]() {
    if (cur.empty())
      return;
    std::string up = cur;
    std::transform(up.begin(), up.end(), up.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    if (up == "CTRL" || up == "CONTROL")
      mods += "<Control>";
    else if (up == "ALT")
      mods += "<Alt>";
    else if (up == "SHIFT")
      mods += "<Shift>";
    else if (up == "SUPER" || up == "META" || up == "WIN")
      mods += "<Super>";
    else
      key = cur;
    cur.clear();
  };
  for (char c : in) {
    if (c == '+')
      flush();
    else
      cur.push_back(c);
  }
  flush();
  return mods + key;
}

// Marshal a callback to the default (GTK) main loop.
struct IdleCb {
  std::function<void()> fn;
};

gboolean dispatchToMainLoop(gpointer data) {
  auto *c = static_cast<IdleCb *>(data);
  try {
    if (c->fn)
      c->fn();
  } catch (...) {
  }
  delete c;
  return G_SOURCE_REMOVE;
}

void postToMainLoop(std::function<void()> fn) {
  auto *c = new IdleCb{std::move(fn)};
  g_idle_add(dispatchToMainLoop, c);
}

} // namespace

// ============================================================================
// Public API  (runs on the caller's thread — non-blocking)
// ============================================================================

PortalShortcutManager::PortalShortcutManager() = default;

PortalShortcutManager::~PortalShortcutManager() { stop(); }

bool PortalShortcutManager::registerShortcut(ShortcutDescriptor desc) {
  if (desc.id.empty())
    return false;
  std::lock_guard<std::mutex> lock(m_mutex);
  m_shortcuts[desc.id] = std::move(desc);
  return true;
}

void PortalShortcutManager::unregisterShortcut(const std::string &id) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_shortcuts.erase(id);
}

bool PortalShortcutManager::start() {
  if (m_thread.joinable())
    return true;

  m_stopRequested.store(false);
  m_active.store(false);
  m_thread = std::thread(&PortalShortcutManager::workerMain, this);
  return true;
}

void PortalShortcutManager::stop() {
  if (!m_thread.joinable())
    return;

  m_stopRequested.store(true);

  // Quit the worker's main loop from the caller thread. g_main_loop_quit
  // is thread-safe w.r.t. g_main_loop_run.
  if (m_loop)
    g_main_loop_quit(m_loop);

  m_thread.join();
  m_active.store(false);
}

// ============================================================================
// Worker thread
// ============================================================================

void PortalShortcutManager::workerMain() {
  m_ctx = g_main_context_new();
  g_main_context_push_thread_default(m_ctx);
  m_loop = g_main_loop_new(m_ctx, FALSE);

  GSource *src = g_idle_source_new();
  g_source_set_callback(src, &PortalShortcutManager::onStartRequested, this,
                        nullptr);
  g_source_attach(src, m_ctx);
  g_source_unref(src);

  g_main_loop_run(m_loop);

  cleanupOnWorker();

  g_main_context_pop_thread_default(m_ctx);
  g_main_loop_unref(m_loop);
  m_loop = nullptr;
  g_main_context_unref(m_ctx);
  m_ctx = nullptr;
}

gboolean PortalShortcutManager::onStartRequested(gpointer user_data) {
  auto *self = static_cast<PortalShortcutManager *>(user_data);
  if (self->m_stopRequested.load()) {
    g_main_loop_quit(self->m_loop);
    return G_SOURCE_REMOVE;
  }
  g_bus_get(G_BUS_TYPE_SESSION, /*cancellable=*/nullptr,
            &PortalShortcutManager::onBusReady, self);
  return G_SOURCE_REMOVE;
}

void PortalShortcutManager::onBusReady(GObject * /*source*/, GAsyncResult *res,
                                       gpointer user_data) {
  auto *self = static_cast<PortalShortcutManager *>(user_data);

  GError *err = nullptr;
  self->m_conn = g_bus_get_finish(res, &err);
  if (!self->m_conn) {
    std::cerr << "[portal] Failed to connect to session bus: "
              << (err ? err->message : "unknown") << std::endl;
    if (err)
      g_error_free(err);
    g_main_loop_quit(self->m_loop);
    return;
  }

  const std::string handleToken = randomToken("saf_req");
  self->m_sessionToken = randomToken("saf_sess");
  const std::string handlePath = computeHandlePath(self->m_conn, handleToken);

  // Subscribe to the Response signal on the request handle path BEFORE
  // making the call, so we don't race a fast reply.
  self->m_createSessionRespSubId = g_dbus_connection_signal_subscribe(
      self->m_conn, PORTAL_BUS, REQ_IFACE, "Response", handlePath.c_str(),
      /*arg0=*/nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
      &PortalShortcutManager::onCreateSessionResponse, self, nullptr);

  GVariantBuilder opts;
  g_variant_builder_init(&opts, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&opts, "{sv}", "handle_token",
                        g_variant_new_string(handleToken.c_str()));
  g_variant_builder_add(&opts, "{sv}", "session_handle_token",
                        g_variant_new_string(self->m_sessionToken.c_str()));

  g_dbus_connection_call(
      self->m_conn, PORTAL_BUS, PORTAL_PATH, GS_IFACE, "CreateSession",
      g_variant_new("(a{sv})", &opts), G_VARIANT_TYPE("(o)"),
      G_DBUS_CALL_FLAGS_NONE, PORTAL_CALL_TIMEOUT_MS, /*cancellable=*/nullptr,
      &PortalShortcutManager::onCreateSessionReply, self);
}

void PortalShortcutManager::onCreateSessionReply(GObject *source,
                                                 GAsyncResult *res,
                                                 gpointer user_data) {
  auto *self = static_cast<PortalShortcutManager *>(user_data);
  GError *err = nullptr;
  GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source),
                                                  res, &err);
  if (!reply) {
    std::cerr << "[portal] CreateSession call failed: "
              << (err ? err->message : "unknown") << std::endl;
    if (err)
      g_error_free(err);
    g_main_loop_quit(self->m_loop);
    return;
  }
  g_variant_unref(reply);
  // Waiting for the Response signal.
}

void PortalShortcutManager::onCreateSessionResponse(
    GDBusConnection * /*conn*/, const char * /*sender*/, const char * /*path*/,
    const char * /*iface*/, const char * /*signal*/, GVariant *params,
    gpointer user_data) {
  auto *self = static_cast<PortalShortcutManager *>(user_data);

  guint code = 1;
  GVariant *results = nullptr;
  g_variant_get(params, "(u@a{sv})", &code, &results);

  if (self->m_createSessionRespSubId) {
    g_dbus_connection_signal_unsubscribe(self->m_conn,
                                         self->m_createSessionRespSubId);
    self->m_createSessionRespSubId = 0;
  }

  if (code != 0) {
    std::cerr << "[portal] CreateSession cancelled/failed (code=" << code
              << ")." << std::endl;
    if (results)
      g_variant_unref(results);
    g_main_loop_quit(self->m_loop);
    return;
  }

  const char *sessionHandle = nullptr;
  if (!g_variant_lookup(results, "session_handle", "&s", &sessionHandle) ||
      !sessionHandle) {
    std::cerr << "[portal] CreateSession: session_handle missing." << std::endl;
    if (results)
      g_variant_unref(results);
    g_main_loop_quit(self->m_loop);
    return;
  }
  self->m_sessionHandle = sessionHandle;
  g_variant_unref(results);

  self->bindShortcutsLocked();
}

void PortalShortcutManager::bindShortcutsLocked() {
  std::vector<ShortcutDescriptor> snapshot;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto &[id, d] : m_shortcuts)
      snapshot.push_back(d);
  }

  if (snapshot.empty()) {
    std::cerr << "[portal] No shortcuts to bind. Session is alive but idle."
              << std::endl;
    m_active.store(true);
    return;
  }

  GVariantBuilder list;
  g_variant_builder_init(&list, G_VARIANT_TYPE("a(sa{sv})"));
  std::cerr << "[portal] Submitting shortcuts to BindShortcuts:" << std::endl;
  for (const auto &d : snapshot) {
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&props, "{sv}", "description",
                          g_variant_new_string(d.description.c_str()));
    const std::string accel = normalizeAccel(d.defaultAccel);
    g_variant_builder_add(&props, "{sv}", "preferred_trigger",
                          g_variant_new_string(accel.c_str()));
    g_variant_builder_add(&list, "(sa{sv})", d.id.c_str(), &props);
    std::cerr << "  * id=\"" << d.id << "\"  preferred=\"" << accel
              << "\"  desc=\"" << d.description << "\"" << std::endl;
  }

  const std::string handleToken = randomToken("saf_bind");
  const std::string handlePath = computeHandlePath(m_conn, handleToken);

  m_bindShortcutsRespSubId = g_dbus_connection_signal_subscribe(
      m_conn, PORTAL_BUS, REQ_IFACE, "Response", handlePath.c_str(), nullptr,
      G_DBUS_SIGNAL_FLAGS_NONE,
      &PortalShortcutManager::onBindShortcutsResponse, this, nullptr);

  GVariantBuilder opts;
  g_variant_builder_init(&opts, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&opts, "{sv}", "handle_token",
                        g_variant_new_string(handleToken.c_str()));

  g_dbus_connection_call(
      m_conn, PORTAL_BUS, PORTAL_PATH, GS_IFACE, "BindShortcuts",
      g_variant_new("(oa(sa{sv})sa{sv})", m_sessionHandle.c_str(), &list,
                    /*parent_window=*/"", &opts),
      G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, PORTAL_CALL_TIMEOUT_MS,
      /*cancellable=*/nullptr,
      &PortalShortcutManager::onBindShortcutsReply, this);
}

void PortalShortcutManager::onBindShortcutsReply(GObject *source,
                                                 GAsyncResult *res,
                                                 gpointer user_data) {
  auto *self = static_cast<PortalShortcutManager *>(user_data);
  GError *err = nullptr;
  GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source),
                                                  res, &err);
  if (!reply) {
    std::cerr << "[portal] BindShortcuts call failed: "
              << (err ? err->message : "unknown") << std::endl;
    if (err)
      g_error_free(err);
    g_main_loop_quit(self->m_loop);
    return;
  }
  g_variant_unref(reply);
  // Waiting for Response.
}

void PortalShortcutManager::onBindShortcutsResponse(
    GDBusConnection * /*conn*/, const char * /*sender*/, const char * /*path*/,
    const char * /*iface*/, const char * /*signal*/, GVariant *params,
    gpointer user_data) {
  auto *self = static_cast<PortalShortcutManager *>(user_data);

  guint code = 1;
  GVariant *results = nullptr;
  g_variant_get(params, "(u@a{sv})", &code, &results);

  if (self->m_bindShortcutsRespSubId) {
    g_dbus_connection_signal_unsubscribe(self->m_conn,
                                         self->m_bindShortcutsRespSubId);
    self->m_bindShortcutsRespSubId = 0;
  }

  if (code != 0) {
    std::cerr << "[portal] BindShortcuts cancelled/failed (code=" << code
              << ")." << std::endl;
    if (results)
      g_variant_unref(results);
    g_main_loop_quit(self->m_loop);
    return;
  }
  if (results)
    g_variant_unref(results);

  self->m_activatedSubId = g_dbus_connection_signal_subscribe(
      self->m_conn, PORTAL_BUS, GS_IFACE, "Activated", PORTAL_PATH, nullptr,
      G_DBUS_SIGNAL_FLAGS_NONE, &PortalShortcutManager::onActivatedSignal,
      self, nullptr);

  self->m_active.store(true);
  std::cerr << "[portal] Ready. Session=" << self->m_sessionHandle << std::endl;

  // Immediately call ListShortcuts so we can log exactly what the portal
  // (and, on Hyprland, the compositor via xdg-desktop-portal-hyprland)
  // has registered for us. This helps users diagnose why key presses may
  // not actually reach the handler — on Hyprland the user must also add
  // a `bind = <keys>, global, <app_id>:<shortcut_id>` line to hyprland.conf.
  const std::string handleToken = randomToken("saf_list");
  const std::string handlePath = computeHandlePath(self->m_conn, handleToken);
  self->m_listShortcutsRespSubId = g_dbus_connection_signal_subscribe(
      self->m_conn, PORTAL_BUS, REQ_IFACE, "Response", handlePath.c_str(),
      nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
      &PortalShortcutManager::onListShortcutsResponse, self, nullptr);

  GVariantBuilder listOpts;
  g_variant_builder_init(&listOpts, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&listOpts, "{sv}", "handle_token",
                        g_variant_new_string(handleToken.c_str()));

  g_dbus_connection_call(
      self->m_conn, PORTAL_BUS, PORTAL_PATH, GS_IFACE, "ListShortcuts",
      g_variant_new("(oa{sv})", self->m_sessionHandle.c_str(), &listOpts),
      G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, PORTAL_CALL_TIMEOUT_MS,
      /*cancellable=*/nullptr,
      &PortalShortcutManager::onListShortcutsReply, self);
}

void PortalShortcutManager::onListShortcutsReply(GObject *source,
                                                 GAsyncResult *res,
                                                 gpointer user_data) {
  auto *self = static_cast<PortalShortcutManager *>(user_data);
  GError *err = nullptr;
  GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source),
                                                  res, &err);
  if (!reply) {
    // Not fatal — Hyprland portal versions < 1.0 may not implement
    // ListShortcuts. Just skip the diagnostic log.
    if (err)
      g_error_free(err);
    return;
  }
  g_variant_unref(reply);
  // Response will arrive asynchronously in onListShortcutsResponse.
}

void PortalShortcutManager::onListShortcutsResponse(
    GDBusConnection * /*conn*/, const char * /*sender*/, const char * /*path*/,
    const char * /*iface*/, const char * /*signal*/, GVariant *params,
    gpointer user_data) {
  auto *self = static_cast<PortalShortcutManager *>(user_data);

  guint code = 1;
  GVariant *results = nullptr;
  g_variant_get(params, "(u@a{sv})", &code, &results);

  if (self->m_listShortcutsRespSubId) {
    g_dbus_connection_signal_unsubscribe(self->m_conn,
                                         self->m_listShortcutsRespSubId);
    self->m_listShortcutsRespSubId = 0;
  }

  if (code != 0) {
    if (results)
      g_variant_unref(results);
    return;
  }

  GVariant *shortcuts = nullptr;
  if (!g_variant_lookup(results, "shortcuts", "@a(sa{sv})", &shortcuts)) {
    g_variant_unref(results);
    return;
  }

  GVariantIter iter;
  g_variant_iter_init(&iter, shortcuts);
  const char *id = nullptr;
  GVariant *props = nullptr;
  size_t n = 0;
  size_t nWithTrigger = 0;
  std::cerr << "[portal] Bound shortcuts (as reported by the compositor):"
            << std::endl;
  while (g_variant_iter_next(&iter, "(&s@a{sv})", &id, &props)) {
    const char *desc = nullptr;
    const char *trigger = nullptr;
    g_variant_lookup(props, "description", "&s", &desc);
    g_variant_lookup(props, "trigger_description", "&s", &trigger);
    const bool hasTrigger = trigger && *trigger;
    std::cerr << "  * " << (id ? id : "?")
              << "  trigger=" << (hasTrigger ? trigger : "<none>")
              << "  desc=" << (desc ? desc : "") << std::endl;
    g_variant_unref(props);
    ++n;
    if (hasTrigger)
      ++nWithTrigger;
  }
  g_variant_unref(shortcuts);
  g_variant_unref(results);

  // On Hyprland the portal accepts the bindings but reports no trigger
  // until the user adds a matching `bind = ..., global, ...` entry. So
  // "needs user action" really means "no shortcut has a live trigger".
  if (nWithTrigger == 0) {
    std::cerr << "[portal] The compositor has not mapped any key to our "
                 "shortcuts yet." << std::endl;
    std::cerr << "[portal] On Hyprland, add one 'bind = ..., global, ...' "
                 "entry per shortcut to your hyprland.conf:" << std::endl;

    std::vector<ShortcutDescriptor> snap;
    {
      std::lock_guard<std::mutex> lock(self->m_mutex);
      for (const auto &[k, v] : self->m_shortcuts)
        snap.push_back(v);
    }
    for (const auto &d : snap) {
      // Translate our "Ctrl+Alt+K" into Hyprland's bind syntax
      // "CTRL ALT, K" (mods separated by spaces, then ", <key>").
      std::string mods;
      std::string key;
      std::string cur;
      auto flush = [&]() {
        if (cur.empty())
          return;
        std::string up = cur;
        std::transform(up.begin(), up.end(), up.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        if (up == "CTRL" || up == "CONTROL" || up == "ALT" ||
            up == "SHIFT" || up == "SUPER" || up == "META" || up == "WIN") {
          if (!mods.empty())
            mods += ' ';
          mods += (up == "WIN" || up == "META") ? "SUPER" : up;
        } else {
          key = cur;
        }
        cur.clear();
      };
      for (char c : d.defaultAccel) {
        if (c == '+')
          flush();
        else
          cur.push_back(c);
      }
      flush();
      // NOTE: leading ':' is mandatory — xdg-desktop-portal registers
      // non-flatpak apps with an EMPTY app_id, so the arg must start
      // with a colon (NOT 'simpleautoframer:<id>').
      std::cerr << "    bind = " << (mods.empty() ? "" : mods) << ", "
                << (key.empty() ? "<key>" : key)
                << ", global, :" << d.id << std::endl;
    }
    std::cerr << "[portal] After editing hyprland.conf: hyprctl reload"
              << std::endl;
    std::cerr << "[portal] If a shortcut still doesn't fire, clear the "
                 "portal's cached sessions:" << std::endl;
    std::cerr << "           systemctl --user restart "
                 "xdg-desktop-portal-hyprland" << std::endl;
  } else {
    std::cerr << "[portal] " << nWithTrigger << "/" << n
              << " shortcut(s) have a live trigger and will route."
              << std::endl;
  }
}

void PortalShortcutManager::onActivatedSignal(GDBusConnection * /*conn*/,
                                              const char *sender,
                                              const char * /*path*/,
                                              const char * /*iface*/,
                                              const char * /*signal*/,
                                              GVariant *params,
                                              gpointer user_data) {
  auto *self = static_cast<PortalShortcutManager *>(user_data);

  const char *sessionHandle = nullptr;
  const char *shortcutId = nullptr;
  guint64 ts = 0;
  GVariant *opts = nullptr;
  g_variant_get(params, "(&o&st@a{sv})", &sessionHandle, &shortcutId, &ts,
                &opts);
  if (opts)
    g_variant_unref(opts);

  (void)sender;
  if (!shortcutId)
    return;
  if (sessionHandle && self->m_sessionHandle != sessionHandle) {
    std::cerr << "[portal] Activated '" << shortcutId
              << "' ignored (session mismatch — stale from previous run?)."
              << std::endl;
    return;
  }

  self->fireActivated(shortcutId);
}

void PortalShortcutManager::fireActivated(const std::string &shortcutId) {
  std::function<void()> cb;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_shortcuts.find(shortcutId);
    if (it == m_shortcuts.end()) {
      std::cerr << "[portal] Activated '" << shortcutId
                << "' but not in our registry." << std::endl;
      return;
    }
    cb = it->second.callback;
  }
  if (cb)
    postToMainLoop(cb);
}

void PortalShortcutManager::cleanupOnWorker() {
  if (!m_conn)
    return;

  if (m_activatedSubId) {
    g_dbus_connection_signal_unsubscribe(m_conn, m_activatedSubId);
    m_activatedSubId = 0;
  }
  if (m_createSessionRespSubId) {
    g_dbus_connection_signal_unsubscribe(m_conn, m_createSessionRespSubId);
    m_createSessionRespSubId = 0;
  }
  if (m_bindShortcutsRespSubId) {
    g_dbus_connection_signal_unsubscribe(m_conn, m_bindShortcutsRespSubId);
    m_bindShortcutsRespSubId = 0;
  }
  if (m_listShortcutsRespSubId) {
    g_dbus_connection_signal_unsubscribe(m_conn, m_listShortcutsRespSubId);
    m_listShortcutsRespSubId = 0;
  }

  // Close the portal session (fire and forget).
  if (!m_sessionHandle.empty()) {
    g_dbus_connection_call(m_conn, PORTAL_BUS, m_sessionHandle.c_str(),
                           "org.freedesktop.portal.Session", "Close", nullptr,
                           nullptr, G_DBUS_CALL_FLAGS_NONE, 2000, nullptr,
                           nullptr, nullptr);
    m_sessionHandle.clear();
  }

  g_object_unref(m_conn);
  m_conn = nullptr;
}

} // namespace saf
