#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <gtkmm.h>
#include <gtkmm/image.h>
#include <opencv2/core/mat.hpp>

class MainWindow : public Gtk::Window {
public:
    MainWindow();
    virtual ~MainWindow();

    // Widget de prévisualisation (flux vidéo)
    Gtk::Image m_video_image;
    // Méthode pour mettre à jour l'image affichée
    void update_frame(const cv::Mat& frame);
    // Override pour intercepter la fermeture de la fenêtre
    bool on_delete_event(GdkEventAny* any_event) override;

protected:

    // Ajustements pour les contrôles
    Glib::RefPtr<Gtk::Adjustment> m_zoom_adjustment;
    Glib::RefPtr<Gtk::Adjustment> m_smoothing_adjustment;
    Glib::RefPtr<Gtk::Adjustment> m_confidence_adjustment;
    Glib::RefPtr<Gtk::Adjustment> m_zoom_multiplier_adjustment;
    Glib::RefPtr<Gtk::Adjustment> m_width_adjustment;
    Glib::RefPtr<Gtk::Adjustment> m_height_adjustment;
    // (Preview scale supprimé)

    // Widgets de contrôle
    Gtk::Scale m_zoom_scale;           // Zoom de base
    Gtk::Scale m_smoothing_scale;      // Facteur de lissage
    Gtk::Scale m_confidence_scale;     // Confiance de détection
    Gtk::Scale m_zoom_multiplier_scale;// Multiplicateur de zoom
    Gtk::SpinButton m_width_spin;      // Largeur de sortie
    Gtk::SpinButton m_height_spin;     // Hauteur de sortie
    Gtk::ComboBoxText m_model_selection_combo; // Sélection du modèle
    Gtk::Button m_apply_button;        // Bouton "Appliquer"

    // Nouvelle organisation en split view
    Gtk::Paned m_paned;                // Paned vertical pour séparer vidéo et contrôles
    Gtk::Box m_controls_box;           // Conteneur vertical pour les contrôles

    // Handler du bouton "Appliquer"
    void on_apply_clicked();

};

#endif // MAINWINDOW_H
