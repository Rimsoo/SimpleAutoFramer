#include "MainWindow.h"
#include <iostream>
#include <opencv2/opencv.hpp>
#include <gdkmm/pixbuf.h>
#include <cstring>
#include <atomic>

// Déclaration externe des variables globales (définies dans main.cpp)
extern std::atomic<double> smoothing_factor;
extern std::atomic<double> detection_confidence;
extern std::atomic<int> model_selection;
extern std::atomic<double> zoom_base;
extern std::atomic<double> zoom_multiplier;
extern std::atomic<int> target_width;
extern std::atomic<int> target_height;

MainWindow::MainWindow()
    : m_zoom_adjustment(Gtk::Adjustment::create(1.5, 1.0, 3.0, 0.1)),
      m_smoothing_adjustment(Gtk::Adjustment::create(0.1, 0.0, 1.0, 0.01)),
      m_confidence_adjustment(Gtk::Adjustment::create(0.3, 0.0, 1.0, 0.01)),
      m_zoom_multiplier_adjustment(Gtk::Adjustment::create(0.0, 0.0, 1.0, 0.01)),
      m_width_adjustment(Gtk::Adjustment::create(1080, 320, 3840, 1)),
      m_height_adjustment(Gtk::Adjustment::create(720, 240, 2160, 1)),
      m_zoom_scale(m_zoom_adjustment, Gtk::ORIENTATION_HORIZONTAL),
      m_smoothing_scale(m_smoothing_adjustment, Gtk::ORIENTATION_HORIZONTAL),
      m_confidence_scale(m_confidence_adjustment, Gtk::ORIENTATION_HORIZONTAL),
      m_zoom_multiplier_scale(m_zoom_multiplier_adjustment, Gtk::ORIENTATION_HORIZONTAL),
      m_width_spin(m_width_adjustment, 1, 0),
      m_height_spin(m_height_adjustment, 1, 0),
      m_model_selection_combo(),
      m_apply_button("Appliquer"),
      m_paned(Gtk::ORIENTATION_VERTICAL),
      m_controls_box(Gtk::ORIENTATION_VERTICAL)
{
    set_title("AutoFramer");
    set_default_size(800, 600);

    // Partie supérieure : flux vidéo
    m_paned.add1(m_video_image);

    // Organisation des contrôles
    m_controls_box.pack_start(m_zoom_scale, Gtk::PACK_SHRINK, 5);
    m_controls_box.pack_start(m_smoothing_scale, Gtk::PACK_SHRINK, 5);
    m_controls_box.pack_start(m_confidence_scale, Gtk::PACK_SHRINK, 5);
    m_controls_box.pack_start(m_zoom_multiplier_scale, Gtk::PACK_SHRINK, 5);
    m_controls_box.pack_start(m_width_spin, Gtk::PACK_SHRINK, 5);
    m_controls_box.pack_start(m_height_spin, Gtk::PACK_SHRINK, 5);
    m_model_selection_combo.append("0");
    m_model_selection_combo.append("1");
    m_model_selection_combo.set_active_text("0");
    m_controls_box.pack_start(m_model_selection_combo, Gtk::PACK_SHRINK, 5);
    m_controls_box.pack_start(m_apply_button, Gtk::PACK_SHRINK, 5);

    m_paned.add2(m_controls_box);
    add(m_paned);

    // Définir la position par défaut du split (ex. 400 pixels pour la zone vidéo)
    m_paned.set_position(400);

    // Connexion du signal "Appliquer"
    m_apply_button.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_apply_clicked));
    
    signal_delete_event().connect(sigc::mem_fun(*this, &MainWindow::on_delete_event));
    
    show_all_children();
}


// Lorsqu'on clique sur la croix, on cache la fenêtre (minimisation dans la barre)
bool MainWindow::on_delete_event(GdkEventAny* any_event) {
    hide();
    return true; // Empêche la fermeture de l'application
}

void MainWindow::update_frame(const cv::Mat& frame) {
    cv::Mat rgb_frame;
    cv::cvtColor(frame, rgb_frame, cv::COLOR_BGR2RGB);
    // Créer un Gdk::Pixbuf avec allocation mémoire indépendante
    auto pb = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, false, 8, rgb_frame.cols, rgb_frame.rows);
    size_t size = rgb_frame.total() * rgb_frame.elemSize();
    std::memcpy(pb->get_pixels(), rgb_frame.data, size);
    m_video_image.set(pb);
}

MainWindow::~MainWindow() {}

void MainWindow::on_apply_clicked() {
    zoom_base.store(m_zoom_adjustment->get_value());
    smoothing_factor.store(m_smoothing_adjustment->get_value());
    detection_confidence.store(m_confidence_adjustment->get_value());
    zoom_multiplier.store(m_zoom_multiplier_adjustment->get_value());
    target_width.store(static_cast<int>(m_width_adjustment->get_value()));
    target_height.store(static_cast<int>(m_height_adjustment->get_value()));
    model_selection.store(std::stoi(m_model_selection_combo.get_active_text()));

    std::cout << "Paramètres appliqués :" << std::endl;
    std::cout << "  - Zoom de base : " << zoom_base.load() << std::endl;
    std::cout << "  - Facteur de lissage : " << smoothing_factor.load() << std::endl;
    std::cout << "  - Confiance de détection : " << detection_confidence.load() << std::endl;
    std::cout << "  - Multiplicateur de zoom : " << zoom_multiplier.load() << std::endl;
    std::cout << "  - Résolution : " << target_width.load() << "x" << target_height.load() << std::endl;
    std::cout << "  - Modèle sélectionné : " << model_selection.load() << std::endl;
}
