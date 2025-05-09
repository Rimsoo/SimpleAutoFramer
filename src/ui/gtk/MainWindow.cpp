#include "MainWindow.h"
#include "UiFactory.h"
#include "gdk/gdkx.h"
#include <filesystem>
#include <iostream>

#include "X11HotkeyListener.h"

namespace fs = std::filesystem;

MainWindow::MainWindow(BaseObjectType *cobject,
                       const Glib::RefPtr<Gtk::Builder> &builder)
    : Gtk::Window(cobject) {
  // Récupérer les widgets depuis Glade
  builder->get_widget("video_image", m_video_image);
  builder->get_widget("apply_button", m_apply_button);
  builder->get_widget("smoothing_factor", m_smoothing_scale);
  builder->get_widget("zoom_base", m_zoom_scale);
  builder->get_widget("zoom_multiplier", m_zoom_multiplier_scale);
  builder->get_widget("detection_confidence", m_confidence_scale);
  builder->get_widget("target_width", m_width_spin);
  builder->get_widget("target_height", m_height_spin);
  builder->get_widget("model_selection", m_model_selection_combo);
  builder->get_widget("main_panned", m_main_panned);
  builder->get_widget("camera_selection", m_camera_selection_combo);
  builder->get_widget("virtual_camera_selection",
                      m_virtual_camera_selection_combo);
  builder->get_widget("profile_box", m_profile_box);
  builder->get_widget("new_profile", m_new_profile);
  builder->get_widget("delete_profile", m_delete_profile);
  builder->get_widget("about_menu_item", m_about_menu_item);
  builder->get_widget("doc_menu_item", m_doc_menu_item);
  builder->get_widget("switch_view", m_switch_view);
  builder->get_widget("shortcut_entry", m_shortcut_entry);

  // Vérifier que tous les widgets ont été correctement chargés
  if (!m_video_image || !m_apply_button || !m_smoothing_scale ||
      !m_zoom_scale || !m_zoom_multiplier_scale || !m_confidence_scale ||
      !m_width_spin || !m_height_spin || !m_model_selection_combo ||
      !m_main_panned || !m_camera_selection_combo ||
      !m_virtual_camera_selection_combo || !m_profile_box || !m_new_profile ||
      !m_delete_profile || !m_about_menu_item || !m_doc_menu_item ||
      !m_switch_view) {
    std::cerr << "Erreur : certains widgets n'ont pas été trouvés dans le "
                 "fichier Glade."
              << std::endl;
    exit(1);
  }

  // Configuration unique des cellules de rendu pour les ComboBox
  m_model_selection_combo->pack_start(m_columns.name);
  m_profile_box->pack_start(m_columns.name);
  m_camera_selection_combo->pack_start(m_columns.name);
  m_virtual_camera_selection_combo->pack_start(m_columns.name);

  // Connecter les signaux
  m_new_profile->signal_activate().connect(
      [this]() { popupNewProfileDialog(); });
  m_apply_button->signal_clicked().connect(
      sigc::mem_fun(*this, &MainWindow::onApplyClicked));
  m_about_menu_item->signal_activate().connect([this]() {
    showMessage(Gtk::MESSAGE_INFO,
                "Simple AutoFramer\nVersion 1.0\n\nAuteur : @Rimsoo");
  });
  m_doc_menu_item->signal_activate().connect([this]() {
    // Ouvrir la documentation dans le navigateur par défaut sur readme.md
    std::system(
        "xdg-open "
        "https://github.com/Rimsoo/SimpleAutoFramer/blob/main/README.md");
  });
  m_switch_view->signal_activate().connect([this]() {
    m_main_panned->get_orientation() == Gtk::ORIENTATION_HORIZONTAL
        ? m_main_panned->set_orientation(Gtk::ORIENTATION_VERTICAL)
        : m_main_panned->set_orientation(Gtk::ORIENTATION_HORIZONTAL);
  });

  show_all_children();
}

void MainWindow::deleteProfileDialog(const std::string &profile_name) {

  if (profile_name.empty()) {
    showMessage(Gtk::MESSAGE_WARNING, "Warning: Profile name cannot be empty.");
    return;
  }
  if (!profilesManager->profileExists(profile_name)) {
    showMessage(Gtk::MESSAGE_WARNING, "Warning: Profile name does not exist.");
    return;
  }

  std::string message =
      "Are you sure you want to delete the profile '" + profile_name + "' ?";
  if (profilesManager->getProfileList().size() == 1)
    message += "\n\nWarning: This is the last profile. You will revert to the "
               "default profile.";
  else if (profile_name == profilesManager->getCurrentProfileName())
    message += "\n\nWarning: The current profile will be switched to the first "
               "profile in the list.";
  Gtk::MessageDialog dialog(*this, message, false, Gtk::MESSAGE_QUESTION,
                            Gtk::BUTTONS_YES_NO);
  dialog.set_default_response(Gtk::RESPONSE_NO);

  int result = dialog.run();
  dialog.close();

  if (result == Gtk::RESPONSE_NO)
    return;

  profilesManager->deleteProfile(profile_name);
  profilesSetup();
  signalProfileChanged.emit();
}

void MainWindow::popupNewProfileDialog() {
  std::string new_profile_name;
  Gtk::MessageDialog dialog(*this, "New profile", false, Gtk::MESSAGE_QUESTION,
                            Gtk::BUTTONS_OK_CANCEL);
  dialog.set_secondary_text("Enter a profile name:");
  dialog.set_default_response(Gtk::RESPONSE_OK);

  Gtk::Entry entry;
  entry.set_activates_default(true);

  dialog.get_content_area()->pack_start(entry, true, true);
  dialog.show_all_children();

  int result = dialog.run();
  dialog.close();

  if (result == Gtk::RESPONSE_OK)
    new_profile_name = entry.get_text();
  else
    return;

  if (new_profile_name.empty()) {
    showMessage(Gtk::MESSAGE_WARNING, "Warning: Profile name cannot be empty.");
    return m_new_profile->activate();
  } else if (profilesManager->profileExists(new_profile_name)) {
    showMessage(Gtk::MESSAGE_WARNING, "Warning: Profile name already exists.");
    return m_new_profile->activate();
  }

  profilesManager->createProfile(new_profile_name);
  profilesSetup();
  signalProfileChanged.emit();
}

void MainWindow::setProfileManager(ProfileManager *profileManager) {
  this->profilesManager = profileManager;

  setupAppIndicator();
  signalProfileChanged.connect([this]() { setupAppIndicator(); });

  // Setup des raccourcis clavier
  setupShortcuts();
  signalShortcutChanged.connect([this]() { setupShortcuts(); });

  profilesSetup();
}

void MainWindow::profilesSetup() {
  setupProfileMenuItems();
  setupProfileBox();
  setupAdjustments();
  setupModelSelection();
  setupCameraSelection(m_camera_selection_combo,
                       profilesManager->getCameraSelection());
  setupCameraSelection(m_virtual_camera_selection_combo,
                       profilesManager->getVirtualCameraSelection());

  m_width_spin->set_value(profilesManager->getTargetWidth());
  m_height_spin->set_value(profilesManager->getTargetHeight());
  m_shortcut_entry->set_text(profilesManager->getShortcut());
}

void MainWindow::setupProfileMenuItems() {
  // Détruire l'ancien sous-menu
  if (m_delete_profile->has_submenu()) {
    auto old_sub_menu = m_delete_profile->get_submenu();
    for (auto widget : old_sub_menu->get_children()) {
      old_sub_menu->remove(*widget); // Supprime les enfants du sous-menu
    }
    m_delete_profile->unset_submenu(); // Détache l'ancien sous-menu
  }

  // Créer un nouveau sous-menu
  auto sub_menu = Gtk::manage(new Gtk::Menu());
  m_delete_profile->set_submenu(*sub_menu);

  // Ajouter les profils au sous-menu
  for (const auto &profile : profilesManager->getProfileList()) {
    Gtk::MenuItem *item =
        Gtk::manage(new Gtk::MenuItem(Glib::ustring(profile.name)));
    item->signal_activate().connect(
        [this, label = Glib::ustring(profile.name)]() {
          deleteProfileDialog(label);
        });
    sub_menu->append(*item);
  }

  // Afficher le sous-menu
  sub_menu->show_all();
}

void MainWindow::setupProfileBox() {
  auto store = Gtk::ListStore::create(m_columns);
  auto profiles = profilesManager->getProfileList();

  Gtk::TreeModel::Row active_iter;
  for (size_t i = 0; i < profiles.size(); ++i) {
    Gtk::TreeModel::Row row = *store->append();
    row[m_columns.name] = profiles[i].name;
    row[m_columns.id] = i;

    if (profiles[i].name == profilesManager->getCurrentProfileName()) {
      active_iter = row;
    }
  }

  m_profile_box->set_model(store);

  if (active_iter) {
    m_profile_box->set_active(active_iter);
  }

  m_profile_box->signal_changed().connect([this]() {
    auto active_iter = m_profile_box->get_active();
    if (active_iter && profilesManager->getCurrentProfileName() !=
                           active_iter->get_value(m_columns.name)) {
      auto name = active_iter->get_value(m_columns.name);
      profilesManager->switchProfile(name);
      profilesSetup();
      signalProfileChanged.emit();
    }
  });
}

void MainWindow::setupAdjustments() {
  auto setup_scale = [](Gtk::Scale *scale, double min, double max, double step,
                        double value) {
    auto adj = scale->get_adjustment();
    adj->set_lower(min);
    adj->set_upper(max);
    adj->set_step_increment(step);
    adj->set_value(value);
  };

  setup_scale(m_zoom_scale, 1.0, 3.0, 0.1, profilesManager->getZoomBase());
  setup_scale(m_smoothing_scale, 0.0, 1.0, 0.01,
              profilesManager->getSmoothingFactor());
  setup_scale(m_zoom_multiplier_scale, 0.0, 5.0, 0.5,
              profilesManager->getZoomMultiplier());
  setup_scale(m_confidence_scale, 0.0, 1.0, 0.01,
              profilesManager->getDetectionConfidence());

  m_width_spin->get_adjustment()->configure(profilesManager->getTargetWidth(),
                                            320, 3840, 1, 10, 0);
  m_height_spin->get_adjustment()->configure(profilesManager->getTargetHeight(),
                                             240, 2160, 1, 10, 0);
}

void MainWindow::setupModelSelection() {
  Glib::RefPtr<Gtk::ListStore> store = Gtk::ListStore::create(m_columns);
  Gtk::TreeModel::Row row = *store->append();
  row[m_columns.name] = "Haar Cascade (CPU)";
  row[m_columns.id] = 0;
  row = *store->append();
  row[m_columns.name] = "DNN (GPU)";
  row[m_columns.id] = 1;

  m_model_selection_combo->set_model(store);
  m_model_selection_combo->set_active(profilesManager->getModelSelection());
}

void MainWindow::setupCameraSelection(Gtk::ComboBoxText *combo,
                                      int current_selection) {
  Glib::RefPtr<Gtk::ListStore> store = Gtk::ListStore::create(m_columns);

  combo->set_model(store);

  Gtk::TreeModel::Row active_iter;
  auto devices = listVideoDevices();
  for (const auto &device : devices) {
    // Extraire l'ID numérique du périphérique, par exemple "/dev/video3" -> 3
    int id = std::stoi(device.substr(std::string("/dev/video").length()));
    Gtk::TreeModel::Row row = *store->append();
    row[m_columns.name] = device;
    row[m_columns.id] = id;

    if (id == current_selection) {
      combo->set_active(row);
    }
  }
}

std::vector<std::string> MainWindow::listVideoDevices() {
  std::vector<std::string> devices;
  for (const auto &entry : std::filesystem::directory_iterator("/dev")) {
    std::string filename = entry.path().filename().string();
    // Vérifier que le nom commence par "video" et qu'il est de la forme
    // "videoX"
    if (filename.rfind("video", 0) == 0) {
      devices.push_back(entry.path().string());
    }
  }
  return devices;
}

bool MainWindow::on_delete_event(GdkEventAny *anyEvent) {
  if (profilesManager->isShowQuitMessage()) {
    std::string msg = "Simple AutoFramer will now run in the background.\n\nTo "
                      "quit, right-click the tray icon and select 'Quit'.";
    // Show a message dialog with the message and a check box for "Don't show
    // this message again"
    Gtk::MessageDialog dialog(*this, msg, false, Gtk::MESSAGE_INFO,
                              Gtk::BUTTONS_OK_CANCEL);
    dialog.set_default_response(Gtk::RESPONSE_OK);
    dialog.set_modal(true);
    dialog.set_transient_for(*this);

    Gtk::CheckButton check_button("Don't show this message again");
    dialog.get_content_area()->pack_end(check_button, false, false);
    dialog.show_all_children();

    int result = dialog.run();
    dialog.close();
    if (result == Gtk::RESPONSE_CANCEL ||
        result == Gtk::RESPONSE_DELETE_EVENT || result == Gtk::RESPONSE_CLOSE) {
      return true;
    }

    if (check_button.get_active()) {
      // Save the user's preference
      profilesManager->setShowQuitMessage(false);
    }
  }

  hide();
  return true; // Empêche la fermeture de l'application
}

void MainWindow::updateFrame(const cv::Mat &frame) {
  if (!is_visible())
    return;
  cv::Mat rgb_frame;
  cv::cvtColor(frame, rgb_frame, cv::COLOR_BGR2RGB);
  if (rgb_frame.empty()) {
    std::cerr << "Erreur : rgb_frame est vide !" << std::endl;
    return;
  }

  auto pb_original = Gdk::Pixbuf::create_from_data(
      rgb_frame.data, Gdk::COLORSPACE_RGB, false, 8, rgb_frame.cols,
      rgb_frame.rows, rgb_frame.step);

  int container_width = m_main_panned->get_position();
  int container_height = m_main_panned->get_height();

  float ratio =
      static_cast<float>(pb_original->get_width()) / pb_original->get_height();
  int new_width = pb_original->get_width();
  int new_height = pb_original->get_height();

  if (pb_original->get_width() > container_width ||
      pb_original->get_height() > container_height) {
    if (pb_original->get_width() > container_width) {
      new_width = container_width;
      new_height = static_cast<int>(container_width / ratio);
    } else {
      new_height = container_height;
      new_width = static_cast<int>(container_height * ratio);
    }
  }

  auto pb_scaled =
      pb_original->scale_simple(new_width, new_height, Gdk::INTERP_BILINEAR);
  if (m_video_image)
    m_video_image->set(pb_scaled);
  else
    std::cerr << "Erreur : m_video_image n'est pas initialisé." << std::endl;
}

void MainWindow::onApplyClicked() {
  profilesManager->setZoomBase(m_zoom_scale->get_value());
  profilesManager->setSmoothingFactor(m_smoothing_scale->get_value());
  profilesManager->setDetectionConfidence(m_confidence_scale->get_value());
  profilesManager->setZoomMultiplier(m_zoom_multiplier_scale->get_value());

  auto vcam_id =
      (*m_virtual_camera_selection_combo->get_active())[m_columns.id];
  bool reload_virtual_camera =
      profilesManager->getTargetWidth() !=
          static_cast<int>(m_width_spin->get_value()) ||
      profilesManager->getTargetHeight() !=
          static_cast<int>(m_height_spin->get_value()) ||
      profilesManager->getVirtualCameraSelection() != vcam_id;

  profilesManager->setTargetWidth(static_cast<int>(m_width_spin->get_value()));
  profilesManager->setTargetHeight(
      static_cast<int>(m_height_spin->get_value()));
  profilesManager->setVirtualCameraSelection(vcam_id);

  auto camera_id = (*m_camera_selection_combo->get_active())[m_columns.id];
  bool reload_camera = profilesManager->getCameraSelection() != camera_id;
  profilesManager->setCameraSelection(camera_id);

  profilesManager->setModelSelection(
      (*m_model_selection_combo->get_active())[m_columns.id]);
  bool shortcut_changed =
      profilesManager->getShortcut() != m_shortcut_entry->get_text();
  profilesManager->setShortcut(m_shortcut_entry->get_text());

  if (reload_virtual_camera) {
    signalVirtualCameraChanged.emit();
    m_width_spin->set_value(profilesManager->getTargetWidth());
    m_height_spin->set_value(profilesManager->getTargetHeight());
  }
  if (reload_camera) {
    signalCameraChanged.emit();
  }
  if (shortcut_changed) {
    signalShortcutChanged.emit();
  }

  profilesManager->saveProfileFiles();
  profilesSetup();
}

void MainWindow::showMessage(Gtk::MessageType type, const std::string &msg) {
  Gtk::MessageDialog dialog(*this, msg, false, type, Gtk::BUTTONS_OK);
  dialog.run();
}

void MainWindow::setupAppIndicator() {
  static AppIndicator *indicator = nullptr;
  static GtkWidget *menu = nullptr;

#ifdef INSTALL_DATA_DIR
  std::string icon_path =
      std::string(INSTALL_DATA_DIR) + "/simpleautoframer.png";
#else
  std::string icon_path = fs::absolute("simpleautoframer.png").string();
#endif

  if (!fs::exists(icon_path)) {
    showMessage(Gtk::MESSAGE_ERROR, "Erreur : Fichier icône non trouvé.");
    return;
  }

  if (!indicator) {
    indicator = app_indicator_new("simple_auto_framer", icon_path.c_str(),
                                  APP_INDICATOR_CATEGORY_SYSTEM_SERVICES);
    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
  }

  if (menu) {
    gtk_widget_destroy(menu);
  }

  menu = gtk_menu_new();

  GtkWidget *menu_item_label = gtk_menu_item_new_with_label("Profiles");
  gtk_widget_set_sensitive(menu_item_label, FALSE);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item_label);

  std::string currentProfile = profilesManager->getCurrentProfileName();

  GSList *radio_group = nullptr;
  for (const auto &profile : profilesManager->getProfileList()) {
    GtkWidget *menu_item_profile =
        gtk_radio_menu_item_new_with_label(radio_group, profile.name.c_str());
    radio_group =
        gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menu_item_profile));

    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item_profile),
                                   profile.name == currentProfile);

    g_object_set_data_full(G_OBJECT(menu_item_profile), "profile-name",
                           g_strdup(profile.name.c_str()), g_free);

    g_signal_connect(
        menu_item_profile, "activate",
        G_CALLBACK(+[](GtkMenuItem *item, gpointer data) {
          auto win = static_cast<MainWindow *>(data);
          ProfileManager *manager = win->getProfileManager();
          const char *profileName = static_cast<const char *>(
              g_object_get_data(G_OBJECT(item), "profile-name"));
          if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item))) {
            manager->switchProfile(profileName);
            win->profilesSetup();
          }
        }),
        this);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item_profile);
  }

  GtkWidget *separator = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);

  GtkWidget *menu_item_show = gtk_menu_item_new_with_label("Show");
  g_signal_connect(menu_item_show, "activate",
                   G_CALLBACK(+[](GtkMenuItem *, gpointer data) {
                     MainWindow *win = static_cast<MainWindow *>(data);
                     win->present();
                   }),
                   this);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item_show);

  GtkWidget *menu_item_quit = gtk_menu_item_new_with_label("Quit");
  g_signal_connect(menu_item_quit, "activate",
                   G_CALLBACK(+[](GtkMenuItem *, gpointer) {
                     //   close_all();
                     gtk_main_quit();
                     exit(0);
                   }),
                   nullptr);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item_quit);

  gtk_widget_show_all(menu);
  app_indicator_set_menu(indicator, GTK_MENU(menu));
}

void MainWindow::setupShortcuts() {
  auto hotkeys = UiFactory::getHotkeyListener();

  for (const auto &profile : profilesManager->getProfileList()) {
    hotkeys->RegisterHotkey(profile.shortcut, [this, profile]() {
      profilesManager->switchProfile(profile.name);
      profilesSetup();
      signalProfileChanged.emit();
    });
  }
}
