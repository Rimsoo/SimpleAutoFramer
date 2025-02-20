#include "MainWindow.h"
#include "gtk/gtk.h"
#include "gtkmm/enums.h"
#include <iostream>
#include <opencv2/opencv.hpp>
#include <gdkmm/pixbuf.h>
#include <cstring>
#include <atomic>

MainWindow::MainWindow(BaseObjectType* cobject, const Glib::RefPtr<Gtk::Builder>& builder)
    : Gtk::Window(cobject) {
    // Récupérer les widgets
    builder->get_widget("video_image", m_video_image);
    builder->get_widget("apply_button", m_apply_button);
    builder->get_widget("smoothing_factor", m_smoothing_scale);
    builder->get_widget("zoom_base", m_zoom_scale);
    builder->get_widget("zoom_multiplier", m_zoom_multiplier_scale);
    builder->get_widget("detection_confidence", m_confidence_scale);
    builder->get_widget("target_width", m_width_spin);
    builder->get_widget("target_height", m_height_spin);
    builder->get_widget("model_selection", m_model_selection_combo);

    // Vérifier que les widgets ont été correctement récupérés
    if (!m_video_image || !m_apply_button || !m_smoothing_scale || !m_zoom_scale ||
        !m_zoom_multiplier_scale || !m_confidence_scale || !m_width_spin ||
        !m_height_spin || !m_model_selection_combo) {
        std::cerr << "Erreur : certains widgets n'ont pas été trouvés dans le fichier Glade." << std::endl;
        exit(1);
    }

    // Configurer les ajustements et la ComboBox
    setup_adjustments();
    setup_model_selection();

    m_width_spin->set_value(target_width.load());
    m_height_spin->set_value(target_height.load());

    // Connecter le signal "clicked" du bouton "Appliquer" 
    // pour fermer et  re-ouvrir la caméra virtuelle SI les dimensions ont changer, 
    // juste après lappel de la méthode on_apply_clicked
    m_apply_button->signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_apply_clicked));

    // Afficher tous les widgets
    show_all_children();
}

void MainWindow::setup_adjustments() {
    // Configuration des ajustements pour les échelles
    auto setup_scale = [](Gtk::Scale* scale, double min, double max, double step, double value) {
        auto adj = scale->get_adjustment();
        adj->set_lower(min);
        adj->set_upper(max);
        adj->set_step_increment(step);
        adj->set_value(value);
    };

    setup_scale(m_zoom_scale, 1.0, 3.0, 0.1, 1.5);
    setup_scale(m_smoothing_scale, 0.0, 1.0, 0.01, 0.1);
    setup_scale(m_zoom_multiplier_scale, 0.0, 1.0, 0.01, 0.0);
    setup_scale(m_confidence_scale, 0.0, 1.0, 0.01, 0.3);

    // Configuration des SpinButtons
    m_width_spin->get_adjustment()->configure(1080, 320, 3840, 1, 10, 0);
    m_height_spin->get_adjustment()->configure(720, 240, 2160, 1, 10, 0);
}

void MainWindow::setup_model_selection() {
    // Création du modèle pour la ComboBox
    Glib::RefPtr<Gtk::ListStore> store = Gtk::ListStore::create(m_columns);
    Gtk::TreeModel::Row row = *store->append();
    row[m_columns.m_col_id] = "0";
    row = *store->append();
    row[m_columns.m_col_id] = "1";

    // Configuration de la ComboBox
    m_model_selection_combo->set_model(store);
    m_model_selection_combo->pack_start(m_columns.m_col_id);
    m_model_selection_combo->set_active(0);
}

bool MainWindow::on_delete_event(GdkEventAny* any_event) {
    hide();
    return true; // Empêche la fermeture de l'application
}

void MainWindow::update_frame(const cv::Mat& frame) {
    cv::Mat rgb_frame;
    cv::cvtColor(frame, rgb_frame, cv::COLOR_BGR2RGB);
    if (rgb_frame.empty()) {
        std::cerr << "Erreur : rgb_frame est vide !" << std::endl;
        return;
    }

    // Créer un Gdk::Pixbuf avec allocation mémoire indépendante
    auto pb = Gdk::Pixbuf::create(
        Gdk::COLORSPACE_RGB, false, 8,
        rgb_frame.cols, rgb_frame.rows);
    memcpy(pb->get_pixels(), rgb_frame.data, rgb_frame.total() * rgb_frame.elemSize());
    // TODO
    // Créer un Gdk::Pixbuf avec allocation mémoire indépendante mais redimensionné avec la taille de la fenêtre m_video_image
    // auto pb = Gdk::Pixbuf::create_from_data(
    //     rgb_frame.data, Gdk::COLORSPACE_RGB, false, 8,
    //     rgb_frame.cols, rgb_frame.rows, rgb_frame.step);
    // pb = pb->scale_simple(m_video_image->get_allocated_width(), m_video_image->get_allocated_height(), Gdk::INTERP_BILINEAR);    

    // Mettre à jour l'image dans l'interface
    if (m_video_image) {
        m_video_image->set(pb);
    } else {
        std::cerr << "Erreur : m_video_image n'est pas initialisé." << std::endl;
    }
}

void MainWindow::on_apply_clicked() {
    // Récupérer les valeurs des ajustements
    zoom_base.store(m_zoom_scale->get_value());
    smoothing_factor.store(m_smoothing_scale->get_value());
    detection_confidence.store(m_confidence_scale->get_value());
    zoom_multiplier.store(m_zoom_multiplier_scale->get_value());

    // TODO soit recharger la caméra virtuelle ici
    // soit renvoyer les valeurs pour les recharger dans main.cpp si possible
    bool is_size_changed = target_width.load() != static_cast<int>(m_width_spin->get_value()) ||
            target_height.load() != static_cast<int>(m_height_spin->get_value());
    
    target_width.store(static_cast<int>(m_width_spin->get_value()));
    target_height.store(static_cast<int>(m_height_spin->get_value()));

    // Récupérer la sélection du modèle
    if (m_model_selection_combo) {
        auto active_id = m_model_selection_combo->get_active_id();
        if (!active_id.empty()) {
            model_selection.store(std::stoi(active_id));
        } else {
            std::cerr << "Erreur : aucun modèle sélectionné." << std::endl;
        }
    } else {
        std::cerr << "Erreur : m_model_selection_combo n'est pas initialisé." << std::endl;
    }

    // Emit signal if size has changed
    if (is_size_changed) {
        signal_apply_clicked.emit();
        m_width_spin->set_value(target_width.load());
        m_height_spin->set_value(target_height.load());
    }
}

void MainWindow::show_message(Gtk::MessageType type, const std::string& msg) {
    Gtk::MessageDialog dialog(*this, msg, false, type, Gtk::BUTTONS_OK);
    dialog.run();
}
