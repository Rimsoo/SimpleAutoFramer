#include "MainWindow.h"
#include "UiFactory.h"
#include <filesystem>
#include <gdk/gdkkeysyms.h>
#include <glibmm/markup.h>
#include <iostream>

namespace fs = std::filesystem;

MainWindow::MainWindow(BaseObjectType *cobject,
                       const Glib::RefPtr<Gtk::Builder> &builder)
    : Gtk::Window(cobject) {
  // Récupérer les widgets depuis Glade
  builder->get_widget("video_image", m_video_image);
  builder->get_widget("video_container", m_video_container);
  builder->get_widget("apply_button", m_apply_button);
  builder->get_widget("smoothing_factor", m_smoothing_scale);
  builder->get_widget("zoom_base", m_zoom_scale);
  builder->get_widget("zoom_multiplier", m_zoom_multiplier_scale);
  builder->get_widget("detection_confidence", m_confidence_scale);
  builder->get_widget("target_width", m_width_spin);
  builder->get_widget("target_height", m_height_spin);
  builder->get_widget("model_selection", m_model_selection_combo);
  builder->get_widget("camera_selection", m_camera_selection_combo);
  builder->get_widget("virtual_camera_selection",
                      m_virtual_camera_selection_combo);
  builder->get_widget("profile_box", m_profile_box);
  builder->get_widget("new_profile", m_new_profile);
  builder->get_widget("delete_profile", m_delete_profile);
  builder->get_widget("about_menu_item", m_about_menu_item);
  builder->get_widget("doc_menu_item", m_doc_menu_item);
  builder->get_widget("shortcut_entry", m_shortcut_entry);
  builder->get_widget("status_label", m_status_label);

  // Vérifier que tous les widgets ont été correctement chargés
  if (!m_video_image || !m_video_container || !m_apply_button ||
      !m_smoothing_scale || !m_zoom_scale || !m_zoom_multiplier_scale ||
      !m_confidence_scale || !m_width_spin || !m_height_spin ||
      !m_model_selection_combo || !m_camera_selection_combo ||
      !m_virtual_camera_selection_combo || !m_profile_box || !m_new_profile ||
      !m_delete_profile || !m_about_menu_item || !m_doc_menu_item) {
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

  // Faire de l'entrée raccourci un widget cliquable pour capturer les touches.
  m_shortcut_entry->set_editable(false);
  m_shortcut_entry->set_placeholder_text("Cliquez puis appuyez sur la combinaison…");
  m_shortcut_entry->set_icon_from_icon_name("input-keyboard-symbolic",
                                            Gtk::ENTRY_ICON_SECONDARY);
  m_shortcut_entry->set_icon_tooltip_text(
      "Cliquez pour capturer un raccourci", Gtk::ENTRY_ICON_SECONDARY);
  m_shortcut_entry->set_icon_activatable(true, Gtk::ENTRY_ICON_SECONDARY);
  m_shortcut_entry->signal_icon_press().connect(
      [this](Gtk::EntryIconPosition pos, const GdkEventButton *) {
        if (pos == Gtk::ENTRY_ICON_SECONDARY)
          captureShortcut();
      });
  // Un double-clic dans la zone texte déclenche aussi la capture, pratique.
  m_shortcut_entry->signal_button_press_event().connect(
      [this](GdkEventButton *ev) -> bool {
        if (ev->type == GDK_2BUTTON_PRESS) {
          captureShortcut();
          return true;
        }
        return false;
      });

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
  // Live slider preview: updating the in-memory profile is enough, Core
  // reads these values on every frame. We keep the save-on-Apply behaviour.
  auto liveSliderHandler = [this](Gtk::Scale *scale,
                                  void (ProfileManager::*setter)(double)) {
    if (m_suppress_live_updates || !profilesManager)
      return;
    (profilesManager->*setter)(scale->get_value());
  };
  m_zoom_scale->signal_value_changed().connect([this, liveSliderHandler]() {
    liveSliderHandler(m_zoom_scale, &ProfileManager::setZoomBase);
  });
  m_zoom_multiplier_scale->signal_value_changed().connect(
      [this, liveSliderHandler]() {
        liveSliderHandler(m_zoom_multiplier_scale,
                          &ProfileManager::setZoomMultiplier);
      });
  m_smoothing_scale->signal_value_changed().connect(
      [this, liveSliderHandler]() {
        liveSliderHandler(m_smoothing_scale,
                          &ProfileManager::setSmoothingFactor);
      });
  m_confidence_scale->signal_value_changed().connect(
      [this, liveSliderHandler]() {
        liveSliderHandler(m_confidence_scale,
                          &ProfileManager::setDetectionConfidence);
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
  // While we push profile values into the scales/spins, the live slider
  // handlers must NOT re-assign them back onto the profile (which would be
  // a no-op but confusing in traces).
  struct Guard {
    bool &flag;
    bool prev;
    Guard(bool &f) : flag(f), prev(f) { flag = true; }
    ~Guard() { flag = prev; }
  } guard(m_suppress_live_updates);

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

// Called from the capture thread. Must NOT touch GTK. We just copy the
// frame into a mutex-protected buffer and schedule a single idle redraw
// that will run on the GTK thread. If a redraw is already scheduled we
// skip — the next idle handler will pick up the freshest frame, so we
// can never saturate the UI even at 60 fps.
void MainWindow::updateFrame(const cv::Mat &frame) {
  if (frame.empty())
    return;

  cv::Mat rgb;
  cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
  if (rgb.empty())
    return;

  {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    // Ensure the stored Mat owns its data so it outlives the caller.
    m_pendingFrame = rgb.clone();
  }

  bool expected = false;
  if (m_redrawScheduled.compare_exchange_strong(expected, true)) {
    g_idle_add(&MainWindow::onRedrawIdle, this);
  }
}

gboolean MainWindow::onRedrawIdle(gpointer user_data) {
  auto *self = static_cast<MainWindow *>(user_data);
  self->m_redrawScheduled.store(false);
  self->redrawVideo();
  return G_SOURCE_REMOVE;
}

void MainWindow::redrawVideo() {
  if (!m_video_image || !is_visible())
    return;

  // Take a local copy under lock; outside the lock we can do the potentially
  // slow scale operation without blocking the capture thread.
  cv::Mat rgb;
  {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    if (m_pendingFrame.empty())
      return;
    rgb = m_pendingFrame; // cheap: refcounted header only
  }

  // Build a pixbuf that OWNS its pixel buffer. Gdk::Pixbuf::create
  // allocates a new buffer; we then memcpy the rows in. This way the
  // pixbuf is valid for the full lifetime GTK needs it, which fixes the
  // cairo_surface_reference assertion that happened on resize when the
  // original cv::Mat had already been destroyed.
  auto pb = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, /*has_alpha=*/false,
                                /*bits_per_sample=*/8, rgb.cols, rgb.rows);
  if (!pb)
    return;

  const int dst_stride = pb->get_rowstride();
  guint8 *dst = pb->get_pixels();
  const size_t row_bytes = static_cast<size_t>(rgb.cols) * 3;
  for (int y = 0; y < rgb.rows; ++y) {
    std::memcpy(dst + static_cast<size_t>(y) * dst_stride,
                rgb.ptr<uchar>(y), row_bytes);
  }

  // Compute the display size so the image fits the current video area.
  // We look at the box that wraps the image (m_video_container) and give
  // ourselves a small inner margin so the dark card's border is visible.
  int container_width = 0, container_height = 0;
  if (m_video_container) {
    container_width = m_video_container->get_allocated_width() - 24;
    container_height = m_video_container->get_allocated_height() - 24;
  }
  int new_w = pb->get_width();
  int new_h = pb->get_height();
  if (container_width > 0 && container_height > 0 &&
      (new_w > container_width || new_h > container_height)) {
    const float ratio = static_cast<float>(new_w) / new_h;
    if (new_w > container_width) {
      new_w = container_width;
      new_h = static_cast<int>(container_width / ratio);
    } else {
      new_h = container_height;
      new_w = static_cast<int>(container_height * ratio);
    }
  }
  if (new_w < 2 || new_h < 2)
    return;

  // scale_simple returns a new owning pixbuf — safe to hand to GtkImage.
  auto scaled = pb->scale_simple(new_w, new_h, Gdk::INTERP_BILINEAR);
  if (scaled)
    m_video_image->set(scaled);
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

namespace {

// Convert a GDK key event into the SimpleAutoFramer accelerator string
// ("Ctrl+Alt+K", "KP_Add", "F5", …). Returns an empty string for modifier-
// only presses (Ctrl alone, etc.) so the capture dialog can keep listening.
std::string gdkEventToAccel(const GdkEventKey &ev) {
  const guint kv = ev.keyval;

  // Ignore pure-modifier presses.
  switch (kv) {
  case GDK_KEY_Control_L:
  case GDK_KEY_Control_R:
  case GDK_KEY_Shift_L:
  case GDK_KEY_Shift_R:
  case GDK_KEY_Alt_L:
  case GDK_KEY_Alt_R:
  case GDK_KEY_Super_L:
  case GDK_KEY_Super_R:
  case GDK_KEY_Meta_L:
  case GDK_KEY_Meta_R:
  case GDK_KEY_Hyper_L:
  case GDK_KEY_Hyper_R:
  case GDK_KEY_ISO_Level3_Shift: // AltGr
    return "";
  default:
    break;
  }

  std::string out;
  auto append = [&](const char *s) {
    if (!out.empty())
      out += '+';
    out += s;
  };

  const auto mods = ev.state;
  if (mods & GDK_CONTROL_MASK)
    append("Ctrl");
  if (mods & GDK_MOD1_MASK)
    append("Alt");
  if (mods & GDK_SHIFT_MASK)
    append("Shift");
  if (mods & GDK_SUPER_MASK)
    append("Super");

  // Find a readable name for the key. gdk_keyval_name() returns strings like
  // "a", "1", "F5", "KP_Add", "Return", … which matches what X11/Portal
  // accelerator parsers expect.
  const char *keyName = gdk_keyval_name(gdk_keyval_to_upper(kv));
  if (!keyName || *keyName == '\0')
    return "";

  append(keyName);
  return out;
}

} // namespace

void MainWindow::captureShortcut() {
  Gtk::Dialog dialog("Capturer un raccourci", *this, /*modal=*/true);
  dialog.set_transient_for(*this);
  dialog.set_default_size(360, 140);
  dialog.set_resizable(false);

  auto *box = dialog.get_content_area();
  box->set_margin_top(12);
  box->set_margin_bottom(12);
  box->set_margin_start(18);
  box->set_margin_end(18);
  box->set_spacing(8);

  Gtk::Label prompt(
      "Appuyez sur la combinaison de touches désirée.\n"
      "Échap pour annuler, Retour arrière pour effacer.");
  prompt.set_justify(Gtk::JUSTIFY_CENTER);
  prompt.set_line_wrap(true);
  box->pack_start(prompt, true, true);

  Gtk::Label preview;
  preview.set_markup("<b><span size=\"x-large\"> </span></b>");
  preview.set_halign(Gtk::ALIGN_CENTER);
  box->pack_start(preview, true, true);

  dialog.add_button("Annuler", Gtk::RESPONSE_CANCEL);
  auto *okBtn = dialog.add_button("Valider", Gtk::RESPONSE_OK);
  okBtn->set_sensitive(false);

  std::string captured;

  dialog.signal_key_press_event().connect(
      [&](GdkEventKey *ev) -> bool {
        if (ev->keyval == GDK_KEY_Escape) {
          dialog.response(Gtk::RESPONSE_CANCEL);
          return true;
        }
        if (ev->keyval == GDK_KEY_BackSpace &&
            (ev->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SHIFT_MASK |
                          GDK_SUPER_MASK)) == 0) {
          captured.clear();
          preview.set_markup("<b><span size=\"x-large\"> </span></b>");
          okBtn->set_sensitive(false);
          return true;
        }
        std::string accel = gdkEventToAccel(*ev);
        if (accel.empty())
          return true; // modifier-only; keep listening
        captured = accel;
        preview.set_markup("<b><span size=\"x-large\">" +
                           Glib::Markup::escape_text(accel) +
                           "</span></b>");
        okBtn->set_sensitive(true);
        return true;
      },
      /*after=*/false);

  dialog.show_all_children();
  int result = dialog.run();
  dialog.close();

  if (result == Gtk::RESPONSE_OK && !captured.empty()) {
    m_shortcut_entry->set_text(captured);
  }
}

void MainWindow::setupShortcuts() {
  auto *hotkeys = UiFactory::getHotkeyListener();
  if (!hotkeys) {
    std::cerr << "Avertissement : aucun gestionnaire de raccourcis disponible."
              << std::endl;
    return;
  }

  // Drop any previous bindings (the portal backend requires a fresh session).
  hotkeys->UnregisterAll();

  for (const auto &profile : profilesManager->getProfileList()) {
    if (profile.shortcut.empty())
      continue;
    hotkeys->RegisterHotkey(profile.shortcut, [this, profile]() {
      std::cerr << "[saf] shortcut '" << profile.shortcut
                << "' → switching to profile '" << profile.name << "'"
                << std::endl;
      profilesManager->switchProfile(profile.name);
      profilesSetup();
      signalProfileChanged.emit();
    });
  }

  // Activate the batch. On Wayland/portal this triggers the user-visible
  // consent prompt; on X11 it calls XGrabKey.
  const auto backend = hotkeys->Commit();
  if (backend.empty()) {
    std::cerr << "Avertissement : impossible d'activer les raccourcis "
                 "globaux."
              << std::endl;
  }

  // Update status strip with the backend info so the user can see which
  // flavour of shortcut routing is active.
  if (m_status_label) {
    std::string line = "● READY";
    if (!backend.empty())
      line += " · SHORTCUTS " + std::string(backend);
    m_status_label->set_text(line);
  }
}
