#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ProfileManager.h"
#include "glibmm/ustring.h"
#include "gtkmm/comboboxtext.h"
#include "gtkmm/entry.h"
#include "gtkmm/enums.h"
#include "gtkmm/menuitem.h"
#include "gtkmm/paned.h"
#include <boost/algorithm/string.hpp>
#include <config.h>
#include <cstdlib>
#include <cstring>
#include <gdkmm/pixbuf.h>
#include <gtk/gtk.h>
#include <gtkmm.h>
#include <gtkmm/application.h>
#include <libayatana-appindicator/app-indicator.h>
#include <opencv2/dnn.hpp>
#include <opencv2/opencv.hpp>
#include <string>
#include <sys/stat.h>
#include <vector>

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

class MainWindow : public Gtk::Window {

public:
  MainWindow(BaseObjectType *cobject,
             const Glib::RefPtr<Gtk::Builder> &builder);
  virtual ~MainWindow() = default;

  sigc::signal<void> signalShortcutChanged;
  sigc::signal<void> signalVirtualCameraChanged;
  sigc::signal<void> signalCameraChanged;
  sigc::signal<void> signalProfileChanged;

  void updateFrame(const cv::Mat &frame);
  void showMessage(Gtk::MessageType type, const std::string &msg);
  void setProfileManager(ProfileManager *configManager);
  ProfileManager *getProfileManager() { return profilesManager; }
  void profilesSetup();

protected:
  // Structure pour le modèle de ComboBox
  class ModelColumns : public Gtk::TreeModel::ColumnRecord {
  public:
    ModelColumns() {
      add(id);
      add(name);
    }

    Gtk::TreeModelColumn<int> id;
    Gtk::TreeModelColumn<Glib::ustring> name;
  };

  Display *display = nullptr;
  std::map<std::pair<KeyCode, unsigned int>, std::string> shortcutMap;
  ProfileManager *profilesManager;
  // Widgets
  Gtk::Image *m_video_image;
  Gtk::Paned *m_main_panned;
  Gtk::Scale *m_smoothing_scale;
  Gtk::Scale *m_zoom_scale;
  Gtk::Scale *m_zoom_multiplier_scale;
  Gtk::Scale *m_confidence_scale;
  Gtk::SpinButton *m_width_spin;
  Gtk::SpinButton *m_height_spin;
  Gtk::ComboBoxText *m_model_selection_combo;
  Gtk::ComboBoxText *m_camera_selection_combo;
  Gtk::ComboBoxText *m_virtual_camera_selection_combo;
  Gtk::Entry *m_shortcut_entry;
  Gtk::ComboBoxText *m_profile_box;
  Gtk::Button *m_apply_button;

  Gtk::MenuItem *m_new_profile;
  Gtk::MenuItem *m_delete_profile;
  Gtk::MenuItem *m_about_menu_item;
  Gtk::MenuItem *m_doc_menu_item;
  Gtk::MenuItem *m_switch_view;

  // Modèle pour la ComboBox
  ModelColumns m_columns;

  // Méthodes d'initialisation
  void setupProfileMenuItems();
  void popupNewProfileDialog();
  void deleteProfileDialog(const std::string &profileName);
  void setupAdjustments();
  void setupProfileBox();
  void setupModelSelection();
  void setupCameraSelection(Gtk::ComboBoxText *combo, int currentSelection);

  std::vector<std::string> listVideoDevices();

  void setupShortcuts();
  void setupAppIndicator();
  // Gestionnaires de signaux
  bool on_delete_event(GdkEventAny *anyEvent) override;
  void onApplyClicked();
};

#endif // MAINWINDOW_H
