#include <gtkmm.h>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <atomic>
#include <cstdlib>
#include <libayatana-appindicator/app-indicator.h>
#include <gtk/gtk.h>
#include <filesystem>
#include <sys/stat.h>
#include "MainWindow.h"
#include "V4l2Output.h"
#include "V4l2Device.h"
#include <config.h>

#define CAM_WIDTH 720
#define CAM_HEIGHT 480

namespace fs = std::filesystem;

V4l2Output* videoOutput = nullptr;

// Paramètres configurables (atomiques pour la synchronisation entre threads)
std::atomic<double> smoothing_factor(0.1);   
std::atomic<double> detection_confidence(0.3); 
std::atomic<int> model_selection(0);           
std::atomic<double> zoom_base(1.5);            
std::atomic<double> zoom_multiplier(0.0);      
std::atomic<int> target_width(CAM_WIDTH);           
std::atomic<int> target_height(CAM_HEIGHT);           

// Variables d'état
std::atomic<cv::Point2f> last_center({0.5f, 0.5f}); // Centre normalisé
std::atomic<double> last_zoom(1.5);    // Dernier niveau de zoom

// Function declaration
void open_virtual_camera() 
{
    // Close the previous camera if it exists, 
    // check if the object is memory allocated and opened
    delete videoOutput;
    double fps = 30.0;
    const char* outputFile = "/dev/video20";
     // --- Configuration du périphérique virtuel ---
    V4L2DeviceParameters param(outputFile, v4l2_fourcc('B', 'G', 'R', '4'),
    CAM_WIDTH, CAM_HEIGHT, fps, IOTYPE_MMAP);
    videoOutput = V4l2Output::create(param);
    if (!videoOutput) {
        std::cerr << "Erreur : Impossible d'ouvrir la sortie " << outputFile << std::endl;
        return;
    }
}

// Lissage d'une valeur
template <typename T>
T smoothValue(const T &current, const T &last, double factor) {
  return factor * current + (1 - factor) * last;
}

bool file_exists(const std::string& filename) {
    struct stat buffer;
    return (stat(filename.c_str(), &buffer) == 0);
}

// --- AppIndicator ---
// Configuration de l'indicateur système
void setup_app_indicator(MainWindow* main_window) {
    #ifdef INSTALL_DATA_DIR
        std::string icon_path = std::string(INSTALL_DATA_DIR) + "/icon.png";
    #else
        std::string icon_path = fs::absolute("icon.png").string();
    #endif
    if (!fs::exists(icon_path)) {
        std::cerr << "Fichier icône non trouvé : " << icon_path << std::endl;
    }
    AppIndicator *indicator = app_indicator_new("simple_auto_framer", icon_path.c_str(), APP_INDICATOR_CATEGORY_SYSTEM_SERVICES);
    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);

    GtkWidget *menu = gtk_menu_new();

    // Élément "Show"
    GtkWidget *menu_item_show = gtk_menu_item_new_with_label("Show");
    g_signal_connect(menu_item_show, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer data){
        MainWindow* win = static_cast<MainWindow*>(data);
        win->present();
    }), main_window);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item_show);

    // Élément "Quit"
    GtkWidget *menu_item_quit = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(menu_item_quit, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer){
        delete videoOutput;
        gtk_main_quit();
        exit(0);
    }), nullptr);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item_quit);

    gtk_widget_show_all(menu);
    app_indicator_set_menu(indicator, GTK_MENU(menu));
}

int main(int argc, char *argv[]) {
    auto app = Gtk::Application::create(argc, argv, "org.auto_framer");

    // Charger l'interface depuis Glade
    auto refBuilder = Gtk::Builder::create();
    try {
    #ifdef INSTALL_DATA_DIR
        std::string glade_path = std::string(INSTALL_DATA_DIR) + "/main_window.glade";
    #else
        std::string glade_path = fs::absolute("main_window.glade").string();
    #endif
        refBuilder->add_from_file(glade_path);
    } catch (const Glib::Error& ex) {
        std::cerr << "Erreur lors du chargement de l'interface : " << ex.what() << std::endl;
        return 1;
    }

    // Créer la fenêtre principale
    MainWindow* main_window = nullptr;
    refBuilder->get_widget_derived("main_window", main_window);
    if (!main_window) {
        std::cerr << "Erreur : la fenêtre principale n'a pas été trouvée dans le fichier Glade." << std::endl;
        return 1;
    }

    // Initialiser GTK et l'AppIndicator
    gtk_init(nullptr, nullptr);
    setup_app_indicator(main_window);

    // Connect the signal from MainWindow to handle reopening the virtual camera
    main_window->signal_apply_clicked.connect(sigc::ptr_fun(&open_virtual_camera));

    // Ouvrir la webcam (adapter l'index si besoin)
    cv::VideoCapture cap(1);
    if (!cap.isOpened()) {
        std::cerr << "Erreur : Impossible d'ouvrir la webcam." << std::endl;
        return -1;
    }

    // Charger le classificateur Haarcascade pour la détection faciale

    #ifdef INSTALL_DATA_DIR
        std::string cascade_path = std::string(INSTALL_DATA_DIR) + "/haarcascade_frontalface_default.xml";
    #else
        std::string cascade_path = fs::absolute("haarcascade_frontalface_default.xml").string();
    #endif
    cv::CascadeClassifier face_cascade;
    if (!face_cascade.load(cascade_path)) {
        std::cerr << "Erreur : Impossible de charger le modèle de détection faciale." << std::endl;
        return -1;
    }

    // Vérifier l'existence du périphérique virtuel
    if (!fs::exists("/dev/video20"))
        std::cerr << "Attention : /dev/video20 n'existe pas. Vérifiez v4l2loopback." << std::endl;

    // Ouvrir la caméra virtuelle
    open_virtual_camera();

    // Timeout pour traiter une image environ toutes les 33 ms (~30 fps)
    Glib::signal_timeout().connect([&]() -> bool {
        cv::Mat frame;
        if (!cap.read(frame)) {
            std::cerr << "Erreur de capture vidéo." << std::endl;
            return true;
        }

        // Conversion en BGRA pour la détection
        cv::Mat processedBGRA;
        cv::cvtColor(frame, processedBGRA, cv::COLOR_BGR2BGRA);

        // Détection de visage
        std::vector<cv::Rect> faces;
        face_cascade.detectMultiScale(processedBGRA, faces, 1.1, 3, 0, cv::Size(100, 100));

        if (!faces.empty()) {
            cv::Rect face = faces[0];
            cv::Point2f current_center(
                (face.x + face.width / 2.0f) / static_cast<float>(frame.cols),
                (face.y + face.height / 2.0f) / static_cast<float>(frame.rows)
            );
            // Charge, modifie et réécrit last_center (membre statique de MainWindow)
            cv::Point2f old_center = last_center;
            old_center.x = smoothValue(current_center.x, old_center.x, smoothing_factor.load());
            old_center.y = smoothValue(current_center.y, old_center.y, smoothing_factor.load());
            last_center = old_center;

            double face_size = std::max(face.width, face.height);
            double current_zoom = zoom_base.load() + (face_size / static_cast<double>(frame.cols)) * zoom_multiplier.load();
            double old_zoom = last_zoom.load();
            last_zoom.store(smoothValue(current_zoom, old_zoom, smoothing_factor.load()));
        }

        // Calcul de la région de recadrage
        int crop_width = static_cast<int>(frame.cols / last_zoom.load());
        int crop_height = static_cast<int>(frame.rows / last_zoom.load());
        int center_x = static_cast<int>(last_center.load().x * frame.cols);
        int center_y = static_cast<int>(last_center.load().y * frame.rows);
        int x1 = std::max(center_x - crop_width / 2, 0);
        int y1 = std::max(center_y - crop_height / 2, 0);
        int x2 = std::min(center_x + crop_width / 2, frame.cols);
        int y2 = std::min(center_y + crop_height / 2, frame.rows);
        cv::Rect crop_region(x1, y1, x2 - x1, y2 - y1);
        cv::Mat cropped_frame = frame(crop_region);
        cv::Mat processed;
        cv::resize(cropped_frame, processed, cv::Size(target_width.load(), target_height.load()));

        // Conversion de l'image traitée en BGRA pour correspondre au format de sortie
        cv::Mat processedOutput;
        cv::cvtColor(processed, processedOutput, cv::COLOR_BGR2BGRA);

        // Mise à jour de l'IHM avec le flux traité
        main_window->update_frame(processed);

        // Envoi de la frame vers la caméra virtuelle
        size_t bufferSize = processedOutput.total() * processedOutput.elemSize();
        char* buffer = reinterpret_cast<char*>(processedOutput.data);

        timeval timeout;
        bool isWritable = videoOutput->isWritable(&timeout);
        if (isWritable) 
        {
            size_t nb = videoOutput->write(buffer, bufferSize);
            if (nb != bufferSize) 
                std::cerr << "Erreur : " << nb << " octets écrits sur " << bufferSize << std::endl;
        }

        return true;
    }, 33);

    app->hold();
    int ret = app->run(*main_window);

    // Libérer les ressources
    delete videoOutput;
    delete main_window;
    return ret;
}
