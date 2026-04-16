
// GtkUserInterface.cpp
#include "GtkUi.h"
#include <gdk/gdk.h>
#include <iostream>

namespace fs = std::filesystem;

namespace {

// Loads the app CSS and applies it globally to the default screen.
// Non-fatal if the file isn't found — the UI just falls back to the
// system theme with no custom accents.
void loadAppCss() {
#ifdef INSTALL_DATA_DIR
  const std::string css_path =
      std::string(INSTALL_DATA_DIR) + "/main_window.css";
#else
  const std::string css_path = fs::absolute("main_window.css").string();
#endif
  if (!fs::exists(css_path)) {
    std::cerr << "[saf] CSS not found at " << css_path
              << " — skipping custom styling." << std::endl;
    return;
  }
  auto provider = Gtk::CssProvider::create();
  try {
    provider->load_from_path(css_path);
  } catch (const Glib::Error &e) {
    std::cerr << "[saf] Failed to load CSS (" << css_path
              << "): " << e.what() << std::endl;
    return;
  }
  auto display = Gdk::Display::get_default();
  if (!display)
    return;
  auto screen = display->get_default_screen();
  Gtk::StyleContext::add_provider_for_screen(
      screen, provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

} // namespace

GtkUi::GtkUi(Core &core)
    : app_(Gtk::Application::create("org.simple_auto_framer")),
      mainWindow_(nullptr) {

  loadAppCss();

  // Chargement de l'interface via Glade
  auto refBuilder = Gtk::Builder::create();
  try {
#ifdef INSTALL_DATA_DIR
    std::string glade_path =
        std::string(INSTALL_DATA_DIR) + "/main_window.glade";
#else
    std::string glade_path = fs::absolute("main_window.glade").string();
#endif
    refBuilder->add_from_file(glade_path);
  } catch (const Glib::Error &ex) {
    std::cerr << "Erreur lors du chargement de l'interface : " << ex.what()
              << std::endl;
    return;
  }

  refBuilder->get_widget_derived("main_window", mainWindow_);
  if (!mainWindow_) {
    std::cerr << "Erreur : la fenêtre principale n'a pas été trouvée dans le "
                 "fichier Glade."
              << std::endl;
    return;
  }
  mainWindow_->setProfileManager(&core.getProfileManager());
  mainWindow_->signalCameraChanged.connect(
      [&core]() { core.openCaptureCamera(); });
  mainWindow_->signalVirtualCameraChanged.connect(
      [&core]() { core.openVirtualCamera(); });

  // register_application throws Gio::DBus::Error if the well-known name is
  // already taken (quick restarts, stuck previous process, …). Treat it as
  // non-fatal — the app can still function, it just won't receive remote
  // GApplication signals.
  try {
    app_->register_application();
  } catch (const Glib::Error &e) {
    std::cerr << "Avertissement : impossible d'enregistrer l'application "
                 "(peut-être une autre instance ?) : "
              << e.what() << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "Avertissement : register_application : " << e.what()
              << std::endl;
  }

  core.setUi(this);
}

GtkApplication *GtkUi::GetGtkApp() const { return app_->gobj(); }

void GtkUi::showMessage(MessageType type, const std::string &message) {
  if (mainWindow_) {
    auto gtkType = type == MessageType::INFO      ? Gtk::MESSAGE_INFO
                   : type == MessageType::WARNING ? Gtk::MESSAGE_WARNING
                                                  : Gtk::MESSAGE_ERROR;
    mainWindow_->showMessage(gtkType, message);
  } else {
    std::cerr << "/!\\ GTK UI NOT INITALIZED /!\\" << std::endl
              << "Message (" << type << "): " << message << std::endl;
  }
}

void GtkUi::updateFrame(const cv::Mat &frame) {
  if (mainWindow_) {
    mainWindow_->updateFrame(frame);
  }
}

int GtkUi::run() {
  if (!mainWindow_) {
    std::cerr << "Erreur: fenêtre principale non initialisée." << std::endl;
    return 1;
  }

  // Lancer la boucle principale GTK
  app_->hold();
  return app_->run(*mainWindow_);
}
