#include <gtkmm.h>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <atomic>
#include <cstdlib>
#include <libayatana-appindicator/app-indicator.h>
#include <gtk/gtk.h>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include "MainWindow.h"
#include "V4l2Output.h"
#include "V4l2Device.h"
#include "gtkmm/enums.h"
#include <config.h>

#define CAM_WIDTH 1280
#define CAM_HEIGHT 720

namespace fs = std::filesystem;

MainWindow* main_window = nullptr;
double fps = 30.0;
const char* outputFile = "/dev/video20";

// Périphérique de sortie virtuel et capture webcam
V4l2Output* videoOutput = nullptr;
cv::VideoCapture cap(1);

// Paramètres configurables (atomiques pour la synchronisation)
std::atomic<double> smoothing_factor(0.1);
std::atomic<double> detection_confidence(0.3);
std::atomic<int> model_selection(0);       // 0 : Haar Cascade, 1 : DNN (GPU)
std::atomic<double> zoom_base(1.5);
std::atomic<double> zoom_multiplier(0.0);
std::atomic<int> target_width(CAM_WIDTH);
std::atomic<int> target_height(CAM_HEIGHT);

// Variables d'état pour le suivi
std::atomic<cv::Point2f> last_center({0.5f, 0.5f});
std::atomic<double> last_zoom(1.5);

// Modèles de détection globaux
cv::CascadeClassifier face_cascade;
cv::dnn::Net face_net; // Pour le modèle DNN GPU

// Ouvre (ou réouvre) la caméra virtuelle
void open_virtual_camera() 
{
    if (!fs::exists(outputFile))
        main_window->show_message(Gtk::MESSAGE_WARNING, ("Attention : " + std::string(outputFile) + " n'existe pas. Vérifiez v4l2loopback.").c_str());
    
    if (videoOutput != nullptr) 
        videoOutput->stop();
    delete videoOutput;
    videoOutput = nullptr;

    V4L2DeviceParameters paramOut(outputFile, v4l2_fourcc('B', 'G', 'R', '4'),
        target_width.load(), target_height.load(), fps, IOTYPE_MMAP);
    videoOutput = V4l2Output::create(paramOut);
    if (!videoOutput) {
        main_window->show_message(Gtk::MESSAGE_ERROR, ("Erreur : Impossible d'ouvrir la sortie " + std::string(outputFile)).c_str());
        exit(-1);
    }

    if (target_width.load() != videoOutput->getWidth() || target_height.load() != videoOutput->getHeight())
    {
        main_window->show_message(Gtk::MESSAGE_WARNING, "La résolution demandée n'a pas été appliquée.");
        target_width.store(videoOutput->getWidth());
        target_height.store(videoOutput->getHeight());
    }
}

void signal_handler(int signum) {
    if (videoOutput) {
        videoOutput->stop();
        delete videoOutput;
        videoOutput = nullptr;
    }
    if (cap.isOpened()) {
        cap.release();
    }
    exit(0);
}

// Fonction de lissage d'une valeur (pour la stabilité du suivi)
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
void setup_app_indicator() {
    #ifdef INSTALL_DATA_DIR
        std::string icon_path = std::string(INSTALL_DATA_DIR) + "/icon.png";
    #else
        std::string icon_path = fs::absolute("icon.png").string();
    #endif
    if (!fs::exists(icon_path)) {
        main_window->show_message(Gtk::MESSAGE_ERROR, "Erreur : Fichier icône non trouvé.");
    }
    AppIndicator *indicator = app_indicator_new("simple_auto_framer", icon_path.c_str(), APP_INDICATOR_CATEGORY_SYSTEM_SERVICES);
    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);

    GtkWidget *menu = gtk_menu_new();

    GtkWidget *menu_item_show = gtk_menu_item_new_with_label("Show");
    g_signal_connect(menu_item_show, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer data){
        MainWindow* win = static_cast<MainWindow*>(data);
        win->present();
    }), main_window);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item_show);

    GtkWidget *menu_item_quit = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(menu_item_quit, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer){
        signal_handler(SIGINT);
        gtk_main_quit();
        exit(0);
    }), nullptr);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item_quit);

    gtk_widget_show_all(menu);
    app_indicator_set_menu(indicator, GTK_MENU(menu));
}

int main(int argc, char *argv[]) {
    signal(SIGINT, signal_handler);
    auto app = Gtk::Application::create(argc, argv, "org.auto_framer");

    // Chargement de l'interface via Glade
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

    refBuilder->get_widget_derived("main_window", main_window);
    if (!main_window) {
        std::cerr << "Erreur : la fenêtre principale n'a pas été trouvée dans le fichier Glade." << std::endl;
        return 1;
    }

    gtk_init(nullptr, nullptr);
    setup_app_indicator();
    main_window->signal_apply_clicked.connect(sigc::ptr_fun(&open_virtual_camera));

    if (!cap.isOpened()) {
        main_window->show_message(Gtk::MESSAGE_ERROR, "Erreur : Impossible d'ouvrir la webcam.");
        return -1;
    }

    // --- Chargement des modèles de détection ---
#ifdef INSTALL_DATA_DIR
    std::string cascade_path = std::string(INSTALL_DATA_DIR) + "/haarcascade_frontalface_default.xml";
#else
    std::string cascade_path = fs::absolute("haarcascade_frontalface_default.xml").string();
#endif
if (!face_cascade.load(cascade_path)) {
    main_window->show_message(Gtk::MESSAGE_ERROR, "Erreur : Impossible de charger le modèle Haar Cascade.");
    return -1;
}
    
// Chargement du modèle DNN res10_300x300_ssd_iter_140000
#ifdef INSTALL_DATA_DIR
    std::string dnn_proto_path = std::string(INSTALL_DATA_DIR) + "/deploy.prototxt";
    std::string dnn_model_path = std::string(INSTALL_DATA_DIR) + "/res10_300x300_ssd_iter_140000.caffemodel";
#else
    std::string dnn_proto_path = fs::absolute("deploy.prototxt").string();
    std::string dnn_model_path = fs::absolute("res10_300x300_ssd_iter_140000.caffemodel").string();
#endif
    try {
        face_net = cv::dnn::readNetFromCaffe(dnn_proto_path, dnn_model_path);
        face_net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
        face_net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
    } catch (const cv::Exception& ex) {
        main_window->show_message(Gtk::MESSAGE_WARNING, "Avertissement : Echec de l'initialisation du modèle DNN. Seul le modèle Haar Cascade sera utilisé.");
    }

    // Ouvrir la caméra virtuelle
    open_virtual_camera();

    // Traitement en boucle (environ toutes les 33ms)
    Glib::signal_timeout().connect([&]() -> bool {
        cv::Mat frame;
        if (!cap.read(frame)) {
            main_window->show_message(Gtk::MESSAGE_ERROR, "Erreur de capture vidéo.");
            return true;
        }

        std::vector<cv::Rect> faces;
        int current_model = model_selection.load();
        if (current_model == 0) {
            // Détection par Haar Cascade (CPU)
            cv::Mat processedBGRA;
            cv::cvtColor(frame, processedBGRA, cv::COLOR_BGR2BGRA);
            face_cascade.detectMultiScale(processedBGRA, faces, 1.1, 3, 0, cv::Size(100, 100));
        } 
        else if (current_model == 1 && !face_net.empty()) {
            // Détection par DNN (CPU) avec res10_300x300_ssd_iter_140000
            cv::Mat blob = cv::dnn::blobFromImage(frame, 1.0, cv::Size(300, 300),
                                                  cv::Scalar(104.0, 177.0, 123.0), false, false);
            face_net.setInput(blob);
            cv::Mat detections = face_net.forward();
            float conf_threshold = detection_confidence.load();
            
            // Remodélisation de la matrice de détections en 2D (N x 7)
            cv::Mat detectionMat(detections.size[2], detections.size[3], CV_32F, detections.ptr<float>());
            for (int i = 0; i < detectionMat.rows; i++) {
                float confidence = detectionMat.at<float>(i, 2);
                if (confidence > conf_threshold) {
                    int x1 = static_cast<int>(detectionMat.at<float>(i, 3) * frame.cols);
                    int y1 = static_cast<int>(detectionMat.at<float>(i, 4) * frame.rows);
                    int x2 = static_cast<int>(detectionMat.at<float>(i, 5) * frame.cols);
                    int y2 = static_cast<int>(detectionMat.at<float>(i, 6) * frame.rows);
                    faces.push_back(cv::Rect(x1, y1, x2 - x1, y2 - y1));
                }
            }
        }

        if (!faces.empty()) {
            cv::Rect face = faces[0];
            cv::Point2f current_center(
                (face.x + face.width / 2.0f) / static_cast<float>(frame.cols),
                (face.y + face.height / 2.0f) / static_cast<float>(frame.rows)
            );
            cv::Point2f old_center = last_center.load();
            old_center.x = smoothValue(current_center.x, old_center.x, smoothing_factor.load());
            old_center.y = smoothValue(current_center.y, old_center.y, smoothing_factor.load());
            last_center.store(old_center);

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

        cv::Mat processedOutput;
        cv::cvtColor(processed, processedOutput, cv::COLOR_BGR2BGRA);

        size_t bufferSize = processedOutput.total() * processedOutput.elemSize();
        char* buffer = reinterpret_cast<char*>(processedOutput.data);

        // Si aucun consommateur n'est actif, on ne traite pas la frame
        

        size_t nb = videoOutput->write(buffer, bufferSize);
        if (nb != bufferSize)
            std::cerr << "Erreur : " << nb << " octets écrits sur " << bufferSize << std::endl;

        main_window->update_frame(processedOutput);
        return true;
    }, 33);

    app->hold();
    int ret = app->run(*main_window);
    delete videoOutput;
    delete main_window;
    return ret;
}
