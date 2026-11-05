// GtkUserInterface.cpp
#include "GtkUi.h"
#include "Paths.h"
#include <gdk/gdk.h>
#include <iostream>

namespace fs = std::filesystem;

namespace {

// Loads the app CSS and applies it globally to the default screen.
// Non-fatal if the file isn't found — the UI just falls back to the
// system theme with no custom accents.
void loadAppCss() {
  const std::string css_path = saf::resourcePath("main_window.css");
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
    : app_(Gtk::Application::create(SAF_APP_ID)),
      mainWindow_(nullptr) {

  // Single instance: if another SimpleAutoFramer is already running, ask it
  // to show its window and leave before we grab the cameras a second time.
  try {
    app_->register_application();
  } catch (const Glib::Error &e) {
    std::cerr << "Avertissement : impossible d'enregistrer l'application : "
              << e.what() << std::endl;
  }
  if (app_->is_registered() && app_->is_remote()) {
    std::cerr << "[saf] SimpleAutoFramer tourne déjà : affichage de la fenêtre "
                 "existante."
              << std::endl;
    app_->activate();
    std::exit(0);
  }

  loadAppCss();

  // Chargement de l'interface via Glade
  auto refBuilder = Gtk::Builder::create();
  const std::string glade_path = saf::resourcePath("main_window.glade");
  try {
    refBuilder->add_from_file(glade_path);
  } catch (const Glib::Error &ex) {
    std::cerr << "Erreur lors du chargement de l'interface (" << glade_path
              << ") : " << ex.what() << std::endl;
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
  // Profiles can point at different devices / output sizes.
  mainWindow_->signalProfileChanged.connect(
      [&core]() { core.reloadDevicesIfNeeded(); });

  // Relaunching the app (or clicking its launcher) re-shows the window, which
  // is the only way back when no tray is available (GNOME without the
  // AppIndicator extension, bare Hyprland…).
  app_->signal_activate().connect([this]() {
    if (mainWindow_)
      mainWindow_->present();
  });

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
    std::cerr << "Message (" << type << "): " << message << std::endl;
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

  // Keep running when the window is hidden to the tray.
  app_->hold();
  return app_->run(*mainWindow_);
}
