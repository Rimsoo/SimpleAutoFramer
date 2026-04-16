#include "Core.h"
#include "UiFactory.h"
#include <atomic>
#include <config.h>
#include <csignal>
#include <cstdlib>
#include <glib.h>
#include <gtk/gtk.h>
#include <iostream>
#include <opencv2/dnn.hpp>
#include <opencv2/opencv.hpp>
#include <sys/stat.h>

namespace {

// Global pointer to the Core, used by the signal handler for a best-effort
// shutdown. Only atomic flag manipulation is async-signal-safe; the actual
// cleanup happens off the signal frame via a GLib idle callback.
std::atomic<Core *> g_core{nullptr};
std::atomic<bool> g_quit_requested{false};

void request_quit(int sig) {
  // async-signal-safe: just set a flag and post to the main loop.
  g_quit_requested.store(true);
  (void)sig;
}

gboolean quit_on_idle(gpointer) {
  if (auto *core = g_core.load())
    core->stop();
  if (gtk_main_level() > 0)
    gtk_main_quit();
  return G_SOURCE_REMOVE;
}

gboolean poll_quit_flag(gpointer) {
  if (g_quit_requested.exchange(false)) {
    std::cerr << "\n[saf] Arrêt demandé, fermeture propre..." << std::endl;
    quit_on_idle(nullptr);
  }
  return G_SOURCE_CONTINUE;
}

} // namespace

int main(int /*argc*/, char * /*argv*/[]) {
  std::signal(SIGINT, request_quit);
  std::signal(SIGTERM, request_quit);

  Core core;
  g_core.store(&core);

  // Poll the quit flag from the main loop so SIGINT/SIGTERM actually trigger
  // a clean GTK shutdown (Core::stop → release camera → join threads).
  g_timeout_add(200, poll_quit_flag, nullptr);

  UiFactory::createUi(core);
  core.start();

  g_core.store(nullptr);
  return 0;
}
