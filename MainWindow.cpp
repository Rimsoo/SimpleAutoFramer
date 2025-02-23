#include "MainWindow.h"
#include "gtkmm/enums.h"
#include <iostream>
#include <opencv2/opencv.hpp>
#include <gdkmm/pixbuf.h>
#include <cstring>
#include <atomic>

MainWindow::MainWindow(BaseObjectType* cobject, const Glib::RefPtr<Gtk::Builder>& builder)
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

    if (!m_video_image || !m_apply_button || !m_smoothing_scale || !m_zoom_scale ||
        !m_zoom_multiplier_scale || !m_confidence_scale || !m_width_spin ||
        !m_height_spin || !m_model_selection_combo) {
        std::cerr << "Erreur : certains widgets n'ont pas été trouvés dans le fichier Glade." << std::endl;
        exit(1);
    }

    setup_adjustments();
    setup_model_selection();
    setup_camera_selection();

    m_width_spin->set_value(target_width.load());
    m_height_spin->set_value(target_height.load());

    m_apply_button->signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_apply_clicked));
    show_all_children();
}

void MainWindow::setup_adjustments() {
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

    m_width_spin->get_adjustment()->configure(1080, 320, 3840, 1, 10, 0);
    m_height_spin->get_adjustment()->configure(720, 240, 2160, 1, 10, 0);
}

void MainWindow::setup_model_selection() {
    Glib::RefPtr<Gtk::ListStore> store = Gtk::ListStore::create(m_columns);
    Gtk::TreeModel::Row row = *store->append();
    row[m_columns.name] = "Haar Cascade (CPU)";
    row[m_columns.id] = 0;
    row = *store->append();
    row[m_columns.name] = "DNN (GPU)";
    row[m_columns.id] = 1;

    m_model_selection_combo->set_model(store);
    m_model_selection_combo->pack_start(m_columns.name);

    m_model_selection_combo->set_active(model_selection.load());
}

void MainWindow::setup_camera_selection() {
    // Liste des périphériques vidéo dans /dev/video*
    Glib::RefPtr<Gtk::ListStore> store = Gtk::ListStore::create(m_columns);
    for (int i = 0; i < 10; i++) {
        std::string device = "/dev/video" + std::to_string(i);
        if (cv::VideoCapture cap(device); cap.isOpened()) {
            Gtk::TreeModel::Row row = *store->append();
            row[m_columns.name] = device;
            row[m_columns.id] = i;
        }
    }

    m_camera_selection_combo->set_model(store);
    m_camera_selection_combo->pack_start(m_columns.name);

    m_camera_selection_combo->set_active(camera_selection.load());

    m_camera_selection_combo->signal_changed().connect([&]() {
        Gtk::TreeModel::iterator iter = m_camera_selection_combo->get_active();
        if(iter) {
            int selected_id = (*iter)[m_columns.id];
            camera_selection.store(selected_id);
            signal_camera_changed.emit();
        } else {
            show_message(Gtk::MESSAGE_ERROR, "Erreur : Aucune caméra sélectionnée.");
        }
    });
}

bool MainWindow::on_delete_event(GdkEventAny* any_event) {
    hide();
    return true; // Empêche la fermeture de l'application
}

void MainWindow::update_frame(const cv::Mat& frame) {
    if (!is_visible())
        return;
    cv::Mat rgb_frame;
    cv::cvtColor(frame, rgb_frame, cv::COLOR_BGR2RGB);
    if (rgb_frame.empty()) {
        std::cerr << "Erreur : rgb_frame est vide !" << std::endl;
        return;
    }
  
    auto pb_original = Gdk::Pixbuf::create_from_data(
        rgb_frame.data,
        Gdk::COLORSPACE_RGB,
        false,
        8,
        rgb_frame.cols,
        rgb_frame.rows,
        rgb_frame.step
    );
  
    int container_width = m_main_panned->get_position();
    int container_height = m_main_panned->get_height();
  
    float ratio = static_cast<float>(pb_original->get_width()) / pb_original->get_height();
    int new_width = pb_original->get_width();
    int new_height = pb_original->get_height();
    
    if (pb_original->get_width() > container_width || pb_original->get_height() > container_height) {
        if (pb_original->get_width() > container_width) {
            new_width = container_width;
            new_height = static_cast<int>(container_width / ratio);
        } else {
            new_height = container_height;
            new_width = static_cast<int>(container_height * ratio);
            pb_original = pb_original->scale_simple(new_width, new_height, Gdk::INTERP_BILINEAR);
        }
    }
  
    auto pb_scaled = pb_original->scale_simple(new_width, new_height, Gdk::INTERP_BILINEAR);
    if (m_video_image)
        m_video_image->set(pb_scaled);
    else
        std::cerr << "Erreur : m_video_image n'est pas initialisé." << std::endl;
}

void MainWindow::on_apply_clicked() {
    zoom_base.store(m_zoom_scale->get_value());
    smoothing_factor.store(m_smoothing_scale->get_value());
    detection_confidence.store(m_confidence_scale->get_value());
    zoom_multiplier.store(m_zoom_multiplier_scale->get_value());

    bool is_size_changed = target_width.load() != static_cast<int>(m_width_spin->get_value()) ||
                             target_height.load() != static_cast<int>(m_height_spin->get_value());
    
    target_width.store(static_cast<int>(m_width_spin->get_value()));
    target_height.store(static_cast<int>(m_height_spin->get_value()));

    if (m_model_selection_combo) {
        Gtk::TreeModel::iterator iter = m_model_selection_combo->get_active();
        if(iter) {
            int selected_id = (*iter)[m_columns.id];
            model_selection.store(selected_id);
        } else {
            std::cerr << "Erreur : m_model_selection_combo n'a pas de valeur active." << std::endl;
        }
        
    } else {
        std::cerr << "Erreur : m_model_selection_combo n'est pas initialisé." << std::endl;
    }

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
