#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <gtkmm.h>
#include <opencv2/opencv.hpp>
#include <atomic>

class MainWindow : public Gtk::Window {
public:
    MainWindow(BaseObjectType* cobject, const Glib::RefPtr<Gtk::Builder>& builder);
    virtual ~MainWindow() = default;

    void update_frame(const cv::Mat& frame);

protected:
    // Structure pour le modèle de ComboBox
    class ModelColumns : public Gtk::TreeModel::ColumnRecord {
    public:
        ModelColumns() { add(m_col_id); }
        Gtk::TreeModelColumn<Glib::ustring> m_col_id;
    };

    // Widgets
    Gtk::Image* m_video_image;
    Gtk::Scale* m_smoothing_scale;
    Gtk::Scale* m_zoom_scale;
    Gtk::Scale* m_zoom_multiplier_scale;
    Gtk::Scale* m_confidence_scale;
    Gtk::SpinButton* m_width_spin;
    Gtk::SpinButton* m_height_spin;
    Gtk::ComboBox* m_model_selection_combo;
    Gtk::Button* m_apply_button;

    // Modèle pour la ComboBox
    ModelColumns m_columns;

    // Méthodes d'initialisation
    void setup_adjustments();
    void setup_model_selection();

    // Gestionnaires de signaux
    bool on_delete_event(GdkEventAny* any_event) override;
    void on_apply_clicked();

};

#endif // MAINWINDOW_H