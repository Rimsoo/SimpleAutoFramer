
// GtkUserInterface.cpp
#include "GtkUi.h"
#include <iostream>

namespace fs = std::filesystem;

GtkUi::GtkUi(Core &core) : mainWindow_(nullptr) {
  app_ = Gtk::Application::create("org.simple_auto_framer");
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
  mainWindow_->setProfileManager(&core.getProfileManager());
  // Connectez ici d'autres signaux si nécessaire
  mainWindow_->signalCameraChanged.connect(
      [&core]() { core.openCaptureCamera(); });
  mainWindow_->signalVirtualCameraChanged.connect(
      [&core]() { core.openVirtualCamera(); });

  gtk_init(nullptr, nullptr);

  core.setUi(this);
}

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
