#include "MainWindow.h"
#include "ProfileManager.h"
#include "V4l2Device.h"
#include "V4l2Output.h"
#include "gtkmm/enums.h"
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <atomic>
#include <config.h>
#include <cstdlib>
#include <filesystem>
#include <gtk/gtk.h>
#include <gtkmm.h>
#include <iostream>
#include <libayatana-appindicator/app-indicator.h>
#include <mutex>
#include <opencv2/dnn.hpp>
#include <opencv2/opencv.hpp>
#include <random>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unordered_map>
#include "HotkeyListener.h" // Needs to be after Glib imports
#include <boost/algorithm/string.hpp>

namespace fs = std::filesystem;

// Variables d'état pour le suivi
std::atomic<cv::Point2f> last_center({0.5f, 0.5f});
std::atomic<double> last_zoom(1.5);

MainWindow *main_window = nullptr;
double fps = 30.0;

// Périphérique de sortie virtuel et capture webcam
V4l2Output *videoOutput = nullptr;
cv::VideoCapture cap = cv::VideoCapture();

// Modèles de détection globaux
cv::CascadeClassifier face_cascade;
cv::dnn::Net face_net; // Pour le modèle DNN GPU

std::atomic<bool> capturingRunning{false};
std::mutex frameMutex;
cv::Mat sharedFrame;
std::thread captureThread;

void capture_loop() {
  while (capturingRunning) {
    if (cap.isOpened()) {
      cv::Mat frame;
      if (cap.read(frame)) {
        std::lock_guard<std::mutex> lock(frameMutex);
        frame.copyTo(sharedFrame);
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(5)); // Petite pause
  }
}

// Ouvre (ou réouvre) la caméra de capture
void open_capture_camera(ProfileManager &profileManager) {
  if (cap.isOpened())
    cap.release();

  cap.open(profileManager.getCameraSelection());

  if (!cap.isOpened()) {
    main_window->show_message(
        Gtk::MESSAGE_ERROR,
        "Erreur : Impossible d'ouvrir la caméra de capture.\nSelectionnez une "
        "autre caméra dans les paramètres.");
    return;
  }
}

// Ouvre (ou réouvre) la caméra virtuelle
void open_virtual_camera(ProfileManager &profileManager) {
  std::string outputFile =
      "/dev/video" + std::to_string(profileManager.getVirtualCameraSelection());
  if (!fs::exists(outputFile)) {
    main_window->show_message(Gtk::MESSAGE_ERROR,
                              ("Attention : " + std::string(outputFile) +
                               " n'existe pas. Vérifiez v4l2loopback.")
                                  .c_str());
    return;
  }

  if (videoOutput != nullptr)
    videoOutput->stop();
  delete videoOutput;
  videoOutput = nullptr;

  V4L2DeviceParameters paramOut(
      outputFile.c_str(), v4l2_fourcc('B', 'G', 'R', '4'),
      profileManager.getTargetWidth(), profileManager.getTargetHeight(), fps,
      IOTYPE_MMAP);
  videoOutput = V4l2Output::create(paramOut);
  if (!videoOutput) {
    main_window->show_message(
        Gtk::MESSAGE_ERROR,
        ("Erreur : Impossible d'ouvrir la sortie " + std::string(outputFile))
            .c_str());
    return;
  }

  if (profileManager.getTargetWidth() != videoOutput->getWidth() ||
      profileManager.getTargetHeight() != videoOutput->getHeight()) {
    main_window->show_message(Gtk::MESSAGE_WARNING,
                              "La résolution demandée n'a pas été appliquée.");
    profileManager.setTargetWidth(videoOutput->getWidth());
    profileManager.setTargetHeight(videoOutput->getHeight());
  }
}

void close_all() {
  capturingRunning = false;
  if (captureThread.joinable())
    captureThread.join();

  if (videoOutput) {
    videoOutput->stop();
    delete videoOutput;
    videoOutput = nullptr;
  }
  if (cap.isOpened()) {
    cap.release();
  }
}

// Fonction de lissage d'une valeur (pour la stabilité du suivi)
template <typename T>
T smoothValue(const T &current, const T &last, double factor) {
  return factor * current + (1 - factor) * last;
}

bool file_exists(const std::string &filename) {
  struct stat buffer;
  return (stat(filename.c_str(), &buffer) == 0);
}

// --- AppIndicator ---
// Configuration de l'indicateur système
Display *display = nullptr;
std::map<std::pair<KeyCode, unsigned int>, std::string> shortcut_map;

std::vector<std::string> split(const std::string &s, char delimiter) {
  std::vector<std::string> tokens;
  std::string token;
  std::istringstream tokenStream(s);
  while (std::getline(tokenStream, token, delimiter)) {
    tokens.push_back(token);
  }
  return tokens;
}

void setup_shortcuts(ProfileManager *profileManager) {
  auto &hotkeys = HotkeyListener::GetInstance();

  for (const auto &profile : profileManager->getProfileList()) {
    std::vector<std::string> parts = split(profile.shortcut, '+');
    if (parts.empty())
      continue;

    unsigned int modifiers = 0;
    KeySym keysym = 0;

    for (const auto &part : parts) {
      if (boost::algorithm::to_lower_copy(part).compare("ctrl") == 0)
        modifiers |= ControlMask;
      else if (boost::algorithm::to_lower_copy(part).compare("alt") == 0)
        modifiers |= Mod1Mask;
      else if (boost::algorithm::to_lower_copy(part).compare("shift") == 0)
        modifiers |= ShiftMask;
      else if (boost::algorithm::to_lower_copy(part).compare("super") == 0)
        modifiers |= Mod4Mask;
      else
        keysym = XStringToKeysym(part.c_str());
    }

    if (keysym != 0) {
      hotkeys.RegisterHotkey(keysym, modifiers, [=]() {
        profileManager->switchProfile(profile.name);
        main_window->profiles_setup();
        main_window->signal_profile_changed.emit(); 
      });
    }
    else {
      main_window->show_message(Gtk::MESSAGE_WARNING, "Cannot parse the shortcut : "+profile.shortcut);
    }
  }
}

void setup_app_indicator(ProfileManager *profileManager) {
  static AppIndicator *indicator = nullptr;
  static GtkWidget *menu = nullptr;

#ifdef INSTALL_DATA_DIR
  std::string icon_path = std::string(INSTALL_DATA_DIR) + "/icon.png";
#else
  std::string icon_path = fs::absolute("icon.png").string();
#endif

  if (!fs::exists(icon_path)) {
    main_window->show_message(Gtk::MESSAGE_ERROR,
                              "Erreur : Fichier icône non trouvé.");
    return;
  }

  if (!indicator) {
    indicator = app_indicator_new("simple_auto_framer", icon_path.c_str(),
                                  APP_INDICATOR_CATEGORY_SYSTEM_SERVICES);
    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
  }

  if (menu) {
    gtk_widget_destroy(menu);
  }

  menu = gtk_menu_new();

  GtkWidget *menu_item_label = gtk_menu_item_new_with_label("Profiles");
  gtk_widget_set_sensitive(menu_item_label, FALSE);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item_label);

  std::string currentProfile = profileManager->getCurrentProfileName();

  GSList *radio_group = nullptr;
  for (const auto &profile : profileManager->getProfileList()) {
    GtkWidget *menu_item_profile =
        gtk_radio_menu_item_new_with_label(radio_group, profile.name.c_str());
    radio_group =
        gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menu_item_profile));

    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item_profile),
                                   profile.name == currentProfile);

    g_object_set_data_full(G_OBJECT(menu_item_profile), "profile-name",
                           g_strdup(profile.name.c_str()), g_free);

    g_signal_connect(
        menu_item_profile, "activate",
        G_CALLBACK(+[](GtkMenuItem *item, gpointer data) {
          ProfileManager *manager = static_cast<ProfileManager *>(data);
          const char *profileName = static_cast<const char *>(
              g_object_get_data(G_OBJECT(item), "profile-name"));
          if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item))) {
            manager->switchProfile(profileName);
           main_window->profiles_setup();
        }
        }),
        profileManager);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item_profile);
  }

  GtkWidget *separator = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);

  GtkWidget *menu_item_show = gtk_menu_item_new_with_label("Show");
  g_signal_connect(menu_item_show, "activate",
                   G_CALLBACK(+[](GtkMenuItem *, gpointer data) {
                     MainWindow *win = static_cast<MainWindow *>(data);
                     win->present();
                   }),
                   main_window);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item_show);

  GtkWidget *menu_item_quit = gtk_menu_item_new_with_label("Quit");
  g_signal_connect(menu_item_quit, "activate",
                   G_CALLBACK(+[](GtkMenuItem *, gpointer) {
                     close_all();
                     gtk_main_quit();
                     exit(0);
                   }),
                   nullptr);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item_quit);

  gtk_widget_show_all(menu);
  app_indicator_set_menu(indicator, GTK_MENU(menu));
}

int main(int argc, char *argv[]) {
  std::string config_path;
#ifdef INSTALL_DATA_DIR
  // Récupérer le répertoire home de l'utilisateur
  const char *home_dir = getenv("HOME");
  if (!home_dir) {
    std::cerr << "Error: Cannot find HOME directory." << std::endl;
    return 1;
  }

  // Construire le chemin de configuration
  fs::path config_dir = fs::path(home_dir) / ".config" / "simpleautoframer";
  if (!fs::exists(config_dir)) {
    fs::create_directories(config_dir);
    std::cout << "Création du répertoire de configuration : " << config_dir
              << std::endl;
  }

  config_path = (config_dir / "simpleautoframer.config.json").string();
#else
  config_path = fs::absolute("simpleautoframer.config.json").string();
#endif

  ProfileManager profileManager(config_path);

  auto app = Gtk::Application::create(argc, argv, "org.auto_framer");

  // Chargement de l'interface via Glade
  auto refBuilder = Gtk::Builder::create();
  try {
#ifdef INSTALL_DATA_DIR
    std::string glade_path =
        std::string(INSTALL_DATA_DIR) + "/main_window.glade";
#else
    std::string glade_path = fs::absolute("main_window.glade").string();
#endif
    refBuilder->add_from_file(glade_path);
  } catch (const Glib::Error &ex) {
    std::cerr << "Erreur lors du chargement de l'interface : " << ex.what()
              << std::endl;
    return 1;
  }

  refBuilder->get_widget_derived("main_window", main_window);
  if (!main_window) {
    std::cerr << "Erreur : la fenêtre principale n'a pas été trouvée dans le "
                 "fichier Glade."
              << std::endl;
    return 1;
  }
  main_window->setProfileManager(&profileManager);

  gtk_init(nullptr, nullptr);
  setup_app_indicator(&profileManager);

  main_window->signal_profile_changed.connect(
      [&profileManager]() { 
      setup_app_indicator(&profileManager); 
    });

  main_window->signal_camera_changed.connect([&profileManager]() {
    open_capture_camera(
        profileManager); // 42 est le paramètre que vous souhaitez passer
  });
  open_capture_camera(profileManager);
  capturingRunning = true;
  captureThread = std::thread(capture_loop);

  // --- Chargement des modèles de détection ---
#ifdef INSTALL_DATA_DIR
  std::string cascade_path =
      std::string(INSTALL_DATA_DIR) + "/haarcascade_frontalface_default.xml";
#else
  std::string cascade_path =
      fs::absolute("haarcascade_frontalface_default.xml").string();
#endif
  if (!face_cascade.load(cascade_path)) {
    main_window->show_message(
        Gtk::MESSAGE_ERROR,
        "Erreur : Impossible de charger le modèle Haar Cascade.");
    return -1;
  }

// Chargement du modèle DNN res10_300x300_ssd_iter_140000
#ifdef INSTALL_DATA_DIR
  std::string dnn_proto_path =
      std::string(INSTALL_DATA_DIR) + "/deploy.prototxt";
  std::string dnn_model_path = std::string(INSTALL_DATA_DIR) +
                               "/res10_300x300_ssd_iter_140000.caffemodel";
#else
  std::string dnn_proto_path = fs::absolute("deploy.prototxt").string();
  std::string dnn_model_path =
      fs::absolute("res10_300x300_ssd_iter_140000.caffemodel").string();
#endif
  try {
    face_net = cv::dnn::readNetFromCaffe(dnn_proto_path, dnn_model_path);
    face_net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
    face_net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
  } catch (const cv::Exception &ex) {
    main_window->show_message(
        Gtk::MESSAGE_WARNING,
        "Avertissement : Echec de l'initialisation du modèle DNN. Seul le "
        "modèle Haar Cascade sera utilisé.");
  }

  main_window->signal_virtual_camera_changed.connect(
      [&profileManager]() { open_virtual_camera(profileManager); });
  // Ouvrir la caméra virtuelle
  open_virtual_camera(profileManager);

  // Setup des raccourcis clavier
  setup_shortcuts(&profileManager);
  main_window->signal_shortcut_changed.connect(
      [&profileManager]() { setup_shortcuts(&profileManager); });


  // Traitement en boucle (environ toutes les 33ms)
  Glib::signal_timeout().connect(
      [&]() -> bool {
        if (!cap.isOpened() || !videoOutput)
          return true;

        cv::Mat frame;
        {
          std::lock_guard<std::mutex> lock(frameMutex);
          if (sharedFrame.empty())
            return true; // Pas encore de frame disponible
          sharedFrame.copyTo(frame);
        }

        std::vector<cv::Rect> faces;
        int current_model = profileManager.getModelSelection();
        if (current_model == 0) {
          // Détection par Haar Cascade (CPU)
          cv::Mat processedBGRA;
          cv::cvtColor(frame, processedBGRA, cv::COLOR_BGR2BGRA);
          face_cascade.detectMultiScale(processedBGRA, faces, 1.1, 3, 0,
                                        cv::Size(100, 100));
        } else if (current_model == 1 && !face_net.empty()) {
          // Détection par DNN (CPU) avec res10_300x300_ssd_iter_140000
          cv::Mat blob = cv::dnn::blobFromImage(frame, 1.0, cv::Size(300, 300),
                                                cv::Scalar(104.0, 177.0, 123.0),
                                                false, false);
          face_net.setInput(blob);
          cv::Mat detections = face_net.forward();
          float conf_threshold = profileManager.getDetectionConfidence();

          // Remodélisation de la matrice de détections en 2D (N x 7)
          cv::Mat detectionMat(detections.size[2], detections.size[3], CV_32F,
                               detections.ptr<float>());
          for (int i = 0; i < detectionMat.rows; i++) {
            float confidence = detectionMat.at<float>(i, 2);
            if (confidence > conf_threshold) {
              int x1 =
                  static_cast<int>(detectionMat.at<float>(i, 3) * frame.cols);
              int y1 =
                  static_cast<int>(detectionMat.at<float>(i, 4) * frame.rows);
              int x2 =
                  static_cast<int>(detectionMat.at<float>(i, 5) * frame.cols);
              int y2 =
                  static_cast<int>(detectionMat.at<float>(i, 6) * frame.rows);
              faces.push_back(cv::Rect(x1, y1, x2 - x1, y2 - y1));
            }
          }
        }

        if (!faces.empty()) {
          cv::Rect face = faces[0];
          cv::Point2f current_center(
              (face.x + face.width / 2.0f) / static_cast<float>(frame.cols),
              (face.y + face.height / 2.0f) / static_cast<float>(frame.rows));
          cv::Point2f old_center = last_center.load();
          old_center.x = smoothValue(current_center.x, old_center.x,
                                     profileManager.getSmoothingFactor());
          old_center.y = smoothValue(current_center.y, old_center.y,
                                     profileManager.getSmoothingFactor());
          last_center.store(old_center);

          double face_size = std::max(face.width, face.height);
          double current_zoom = profileManager.getZoomBase() +
                                (face_size / static_cast<double>(frame.cols)) *
                                    profileManager.getZoomMultiplier();
          double old_zoom = last_zoom.load();
          last_zoom.store(smoothValue(current_zoom, old_zoom,
                                      profileManager.getSmoothingFactor()));
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
        cv::resize(cropped_frame, processed,
                   cv::Size(profileManager.getTargetWidth(),
                            profileManager.getTargetHeight()));

        cv::Mat processedOutput;
        cv::cvtColor(processed, processedOutput, cv::COLOR_BGR2BGRA);

        size_t bufferSize =
            processedOutput.total() * processedOutput.elemSize();
        char *buffer = reinterpret_cast<char *>(processedOutput.data);

        // Si aucun consommateur n'est actif, on ne traite pas la frame

        size_t nb = videoOutput->write(buffer, bufferSize);
        if (nb != bufferSize)
          std::cerr << "Erreur : " << nb << " octets écrits sur " << bufferSize
                    << std::endl;

        main_window->update_frame(processedOutput);
        return true;
      },
      33);

  app->hold();
  int ret = app->run(*main_window);
  delete videoOutput;
  delete main_window;
  return ret;
}
