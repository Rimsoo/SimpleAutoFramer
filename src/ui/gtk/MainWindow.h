#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ProfileManager.h"
#include "glibmm/ustring.h"
#include "gtkmm/comboboxtext.h"
#include "gtkmm/entry.h"
#include "gtkmm/enums.h"
#include "gtkmm/menuitem.h"
#include "gtkmm/paned.h"
#include <atomic>
#include <config.h>
#include <cstdlib>
#include <cstring>
#include <gdkmm/pixbuf.h>
#include <gtk/gtk.h>
#include <gtkmm.h>
#include <gtkmm/application.h>
#include <libayatana-appindicator/app-indicator.h>
#include <mutex>
#include <opencv2/dnn.hpp>
#include <opencv2/opencv.hpp>
#include <string>
#include <sys/stat.h>
#include <vector>

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

  ProfileManager *profilesManager;

  // Video frame handoff from the capture thread to the GTK thread.
  // The capture thread copies frames into m_pendingFrame (under mutex).
  // A single idle source (tracked via m_redrawScheduled) reads it from
  // the GTK main thread and updates the GtkImage. This is thread-safe and
  // avoids the pixbuf-points-to-freed-Mat class of cairo assertions.
  std::mutex m_frameMutex;
  cv::Mat m_pendingFrame;                // RGB, owned by this class
  std::atomic<bool> m_redrawScheduled{false};
  static gboolean onRedrawIdle(gpointer user_data);
  void redrawVideo();

  // Widgets
  Gtk::Image *m_video_image{nullptr};
  Gtk::Widget *m_video_container{nullptr};  // the box whose allocation
                                            // drives the pixbuf scale.
  Gtk::Scale *m_smoothing_scale{nullptr};
  Gtk::Scale *m_zoom_scale{nullptr};
  Gtk::Scale *m_zoom_multiplier_scale{nullptr};
  Gtk::Scale *m_confidence_scale{nullptr};
  Gtk::SpinButton *m_width_spin{nullptr};
  Gtk::SpinButton *m_height_spin{nullptr};
  Gtk::ComboBoxText *m_model_selection_combo{nullptr};
  Gtk::ComboBoxText *m_camera_selection_combo{nullptr};
  Gtk::ComboBoxText *m_virtual_camera_selection_combo{nullptr};
  Gtk::Entry *m_shortcut_entry{nullptr};
  Gtk::ComboBoxText *m_profile_box{nullptr};
  Gtk::Button *m_apply_button{nullptr};
  Gtk::Label *m_status_label{nullptr};

  Gtk::MenuItem *m_new_profile{nullptr};
  Gtk::MenuItem *m_delete_profile{nullptr};
  Gtk::MenuItem *m_about_menu_item{nullptr};
  Gtk::MenuItem *m_doc_menu_item{nullptr};

  // Live-preview plumbing for the 4 slider controls: the handlers write
  // directly into the ProfileManager (so Core picks up the change on the
  // next frame), but do NOT save to disk — that still happens on Apply.
  // We raise m_suppress_live_updates during programmatic set_value calls
  // (profile switch etc.) to avoid reacting to ourselves.
  bool m_suppress_live_updates{false};

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

  /// Pop a small modal dialog that listens for a key combination and writes
  /// the result into m_shortcut_entry in SimpleAutoFramer format
  /// (e.g. "Ctrl+Alt+K").
  void captureShortcut();
  // Gestionnaires de signaux
  bool on_delete_event(GdkEventAny *anyEvent) override;
  void onApplyClicked();
};

#endif // MAINWINDOW_H
