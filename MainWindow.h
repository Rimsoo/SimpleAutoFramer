#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ProfileManager.h"
#include "gtkmm/comboboxtext.h"
#include "gtkmm/entry.h"
#include "gtkmm/menuitem.h"
#include "gtkmm/paned.h"
#include <gtkmm.h>
#include <opencv2/opencv.hpp>

class MainWindow : public Gtk::Window {
    
public:
    MainWindow(BaseObjectType* cobject, const Glib::RefPtr<Gtk::Builder>& builder);
    virtual ~MainWindow() = default;

    sigc::signal<void> signal_apply_clicked;
    sigc::signal<void> signal_camera_changed;

    void update_frame(const cv::Mat& frame);
    void show_message(Gtk::MessageType type, const std::string& msg);
    void setProfileManager(ProfileManager* configManager);

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

    ProfileManager* profilesManager;
    // Widgets
    Gtk::Image* m_video_image;
    Gtk::Paned* m_main_panned;
    Gtk::Scale* m_smoothing_scale;
    Gtk::Scale* m_zoom_scale;
    Gtk::Scale* m_zoom_multiplier_scale;
    Gtk::Scale* m_confidence_scale;
    Gtk::SpinButton* m_width_spin;
    Gtk::SpinButton* m_height_spin;
    Gtk::ComboBoxText* m_model_selection_combo;
    Gtk::ComboBoxText* m_camera_selection_combo;
    Gtk::ComboBoxText* m_virtual_camera_selection_combo;
    Gtk::ComboBoxText* m_profile_box;
    Gtk::Button* m_apply_button;
    Gtk::MenuItem* m_new_profile;
    Gtk::MenuItem* m_delete_profile;
    Gtk::MenuItem* m_about_menu_item;
    Gtk::MenuItem* m_doc_menu_item;
    Gtk::MenuItem* m_switch_view;
    
    // Modèle pour la ComboBox
    ModelColumns m_columns;

    // Méthodes d'initialisation
    void profiles_setup();
    void setup_profile_menu_items();
    void popup_new_profile_dialog();
    void delete_profile_dialog(const std::string& profile_name);
    void setup_adjustments();
    void setup_profile_box();
    void setup_model_selection();
    void setup_camera_selection(Gtk::ComboBoxText* combo, int current_selection);
    std::vector<std::string> list_video_devices();

    // Gestionnaires de signaux
    bool on_delete_event(GdkEventAny* any_event) override;
    void on_apply_clicked();
};

#endif // MAINWINDOW_H