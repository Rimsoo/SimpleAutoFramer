#include "MainWindow.h"
#include "glibmm/ustring.h"
#include "gtkmm/enums.h"
#include <atomic>
#include <cstring>
#include <filesystem>
#include <gdkmm/pixbuf.h>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <vector>

MainWindow::MainWindow(BaseObjectType *cobject,
                       const Glib::RefPtr<Gtk::Builder> &builder)
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
    builder->get_widget("virtual_camera_selection",m_virtual_camera_selection_combo);
    builder->get_widget("config_name", m_config_name);
    builder->get_widget("config_box", m_config_box);

    if (!m_video_image || !m_apply_button || !m_smoothing_scale ||
        !m_zoom_scale || !m_zoom_multiplier_scale || !m_confidence_scale ||
        !m_width_spin || !m_height_spin || !m_model_selection_combo) {
        std::cerr << "Erreur : certains widgets n'ont pas été trouvés dans le "
                    "fichier Glade."
                << std::endl;
        exit(1);
    }

    m_apply_button->signal_clicked().connect(
        sigc::mem_fun(*this, &MainWindow::on_apply_clicked));
    show_all_children();
}

void MainWindow::setConfigManager(ConfigManager* configManager) {
    this->configManager = configManager;

    config_setup();
}

void MainWindow::config_setup() {
    setup_config_box();
    setup_adjustments();
    setup_model_selection();
    setup_camera_selection(m_camera_selection_combo, 
        configManager->getCameraSelection());
    setup_camera_selection(m_virtual_camera_selection_combo,
        configManager->getVirtualCameraSelection());

    m_width_spin->set_value(configManager->getTargetWidth());
    m_height_spin->set_value(configManager->getTargetHeight());
}

void MainWindow::setup_config_box() {
    auto store = Gtk::ListStore::create(m_columns);
    auto configs = configManager->getConfigList();

    Gtk::TreeModel::Row active_iter;
    for (const auto &name : configs) {
        Gtk::TreeModel::Row row = *store->append();
        row[m_columns.name] = name;
        if (name.compare(configManager->getCurrentConfigName()) == 0)
            active_iter = row;
    }

    m_config_box->set_model(store);
    m_config_box->pack_start(m_columns.name);
    m_config_box->set_active(active_iter);

    m_config_box->signal_changed().connect([this]() {
        auto active_iter = m_config_box->get_active();
        if (active_iter)
        {        
            auto name = (*active_iter)[m_columns.name];
            configManager->switchConfig(name);
            config_setup();
        }
    });
}

void MainWindow::setup_adjustments() {
    auto setup_scale = [](Gtk::Scale *scale, double min, double max, double step,
                            double value) {
        auto adj = scale->get_adjustment();
        adj->set_lower(min);
        adj->set_upper(max);
        adj->set_step_increment(step);
        adj->set_value(value);
    };

    setup_scale(m_zoom_scale, 1.0, 3.0, 0.1, configManager->getZoomBase());
    setup_scale(m_smoothing_scale, 0.0, 1.0, 0.01, configManager->getSmoothingFactor());
    setup_scale(m_zoom_multiplier_scale, 0.0, 5.0, 0.5, configManager->getZoomMultiplier());
    setup_scale(m_confidence_scale, 0.0, 1.0, 0.01, configManager->getDetectionConfidence());

    m_width_spin->get_adjustment()->configure(configManager->getTargetWidth(), 320, 3840, 1, 10, 0);
    m_height_spin->get_adjustment()->configure(configManager->getTargetHeight(), 240, 2160, 1, 10, 0);
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

    m_model_selection_combo->set_active(configManager->getModelSelection());
}

void MainWindow::setup_camera_selection(Gtk::ComboBoxText *combo, int current_selection) {
    Glib::RefPtr<Gtk::ListStore> store = Gtk::ListStore::create(m_columns);

    Gtk::TreeModel::Row active_iter;
    auto devices = list_video_devices();
    for (const auto &device : devices) {
        // Extraire l'ID numérique du périphérique, par exemple "/dev/video3" -> 3
        int id = std::stoi(device.substr(std::string("/dev/video").length()));
        Gtk::TreeModel::Row row = *store->append();
        row[m_columns.name] = device;
        row[m_columns.id] = id;

        if (id == current_selection)
            active_iter = row;
    }

    combo->set_model(store);
    combo->pack_start(m_columns.name);
    combo->set_active(active_iter);
}

std::vector<std::string> MainWindow::list_video_devices() {
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

bool MainWindow::on_delete_event(GdkEventAny *any_event) {
    hide();
    return true; // Empêche la fermeture de l'application
}

void MainWindow::update_frame(const cv::Mat &frame) {
    if (!is_visible())
        return;
    cv::Mat rgb_frame;
    cv::cvtColor(frame, rgb_frame, cv::COLOR_BGR2RGB);
    if (rgb_frame.empty()) {
        std::cerr << "Erreur : rgb_frame est vide !" << std::endl;
        return;
    }

    auto pb_original = Gdk::Pixbuf::create_from_data(
        rgb_frame.data, Gdk::COLORSPACE_RGB, false, 8, rgb_frame.cols,
        rgb_frame.rows, rgb_frame.step);

    int container_width = m_main_panned->get_position();
    int container_height = m_main_panned->get_height();

    float ratio =
        static_cast<float>(pb_original->get_width()) / pb_original->get_height();
    int new_width = pb_original->get_width();
    int new_height = pb_original->get_height();

    if (pb_original->get_width() > container_width ||
        pb_original->get_height() > container_height) {
        if (pb_original->get_width() > container_width) {
            new_width = container_width;
            new_height = static_cast<int>(container_width / ratio);
        } else {
            new_height = container_height;
            new_width = static_cast<int>(container_height * ratio);
            pb_original = pb_original->scale_simple(new_width, new_height,
                                                Gdk::INTERP_BILINEAR);
        }
    }

    auto pb_scaled =
        pb_original->scale_simple(new_width, new_height, Gdk::INTERP_BILINEAR);
    if (m_video_image)
        m_video_image->set(pb_scaled);
    else
        std::cerr << "Erreur : m_video_image n'est pas initialisé." << std::endl;
}

void MainWindow::on_apply_clicked() {
    std::string config_name = m_config_name->get_text();
    if (!config_name.empty())
        configManager->createConfig(config_name);

    configManager->setZoomBase(m_zoom_scale->get_value());
    configManager->setSmoothingFactor(m_smoothing_scale->get_value());
    configManager->setDetectionConfidence(m_confidence_scale->get_value());
    configManager->setZoomMultiplier(m_zoom_multiplier_scale->get_value());

    auto vcam_id =
        (*m_virtual_camera_selection_combo->get_active())[m_columns.id];
    bool reload_virtual_camera =
        configManager->getTargetWidth() != static_cast<int>(m_width_spin->get_value()) ||
        configManager->getTargetHeight() != static_cast<int>(m_height_spin->get_value()) ||
        configManager->getVirtualCameraSelection() != vcam_id;

    configManager->setTargetWidth(static_cast<int>(m_width_spin->get_value()));
    configManager->setTargetHeight(static_cast<int>(m_height_spin->get_value()));
    configManager->setVirtualCameraSelection(vcam_id);

    auto camera_id = (*m_camera_selection_combo->get_active())[m_columns.id];
    bool reload_camera = configManager->getCameraSelection() != camera_id;
    configManager->setCameraSelection(camera_id);

    configManager->setModelSelection((*m_model_selection_combo->get_active())[m_columns.id]);

    if (reload_virtual_camera) {
        signal_apply_clicked.emit();
        m_width_spin->set_value(configManager->getTargetWidth());
        m_height_spin->set_value(configManager->getTargetHeight());
    }
    if (reload_camera) {
        signal_camera_changed.emit();
    }
    
    configManager->saveConfig();
}

void MainWindow::show_message(Gtk::MessageType type, const std::string &msg) {
    Gtk::MessageDialog dialog(*this, msg, false, type, Gtk::BUTTONS_OK);
    dialog.run();
}
