#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ConfigManager.h"
#include "gtkmm/comboboxtext.h"
#include "gtkmm/entry.h"
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
    void setConfigManager(ConfigManager* configManager);

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

    ConfigManager* configManager;
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
    Gtk::Button* m_apply_button;
    Gtk::Entry* m_config_name;
    
    // Modèle pour la ComboBox
    ModelColumns m_columns;

    // Méthodes d'initialisation
    void setup_adjustments();
    void setup_model_selection();
    void setup_camera_selection(Gtk::ComboBoxText* combo, int current_selection);
    std::vector<std::string> list_video_devices();

    // Gestionnaires de signaux
    bool on_delete_event(GdkEventAny* any_event) override;
    void on_apply_clicked();
};

#endif // MAINWINDOW_H