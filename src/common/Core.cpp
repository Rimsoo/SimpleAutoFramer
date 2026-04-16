// Core.cpp
#include "Core.h"
#include <chrono>
#include <config.h>
#include <cstddef>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace {

std::string resourceDir() {
#ifdef INSTALL_DATA_DIR
  return std::string{INSTALL_DATA_DIR};
#else
  return fs::absolute(".").string();
#endif
}

std::string resourcePath(const std::string &name) {
#ifdef INSTALL_DATA_DIR
  return resourceDir() + "/" + name;
#else
  return fs::absolute(name).string();
#endif
}

} // namespace

Core::Core()
    : ui_(nullptr), profilesManager_(ProfileManager()),
      capturingRunning_(false), videoOutput_(CameraFactory::createCamera()),
      lastCenter_({0.5f, 0.5f}), lastZoom_(1.5) {}

bool Core::init() {
  static bool initialized = false;
  if (initialized)
    return true;

  const std::string cascade_path =
      resourcePath("haarcascade_frontalface_default.xml");

  try {
    if (!faceCascade_.load(cascade_path)) {
      const std::string msg =
          "Erreur : impossible de charger le modèle Haar Cascade (" +
          cascade_path + ").";
      if (ui_)
        ui_->showMessage(IUi::ERROR, msg);
      std::cerr << msg << std::endl;
      return false;
    }
  } catch (const cv::Exception &ex) {
    const std::string msg =
        std::string("Exception OpenCV lors du chargement du Haar Cascade : ") +
        ex.what();
    if (ui_)
      ui_->showMessage(IUi::ERROR, msg);
    std::cerr << msg << std::endl;
    return false;
  }

  // Chargement du modèle DNN res10_300x300_ssd_iter_140000
  const std::string dnn_proto_path = resourcePath("deploy.prototxt");
  const std::string dnn_model_path =
      resourcePath("res10_300x300_ssd_iter_140000.caffemodel");

  try {
    faceNet_ = cv::dnn::readNetFromCaffe(dnn_proto_path, dnn_model_path);
#if defined(SAF_HAS_OPENCV_CUDA) && SAF_HAS_OPENCV_CUDA
    faceNet_.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
    faceNet_.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
#else
    faceNet_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    faceNet_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
#endif
  } catch (const cv::Exception &ex) {
    const std::string msg =
        std::string("Erreur lors du chargement du modèle DNN : ") + ex.what();
    if (ui_)
      ui_->showMessage(IUi::WARNING, msg);
    std::cerr << msg << std::endl;
    // Not a fatal error: Haar Cascade is still available.
  }

  initialized = true;
  return true;
}

// Ouvre (ou réouvre) la caméra de capture
void Core::openCaptureCamera() {
  if (cap_.isOpened())
    cap_.release();

  try {
    cap_.open(profilesManager_.getCameraSelection());
  } catch (const cv::Exception &ex) {
    const std::string msg = std::string("Erreur OpenCV à l'ouverture de la "
                                        "caméra : ") +
                            ex.what();
    if (ui_)
      ui_->showMessage(IUi::ERROR, msg);
    std::cerr << msg << std::endl;
    return;
  }

  if (!cap_.isOpened()) {
    auto msg = "Erreur : Impossible d'ouvrir la caméra de "
               "capture.\nSelectionnez une autre caméra dans les paramètres.";
    if (ui_)
      ui_->showMessage(IUi::ERROR, msg);

    std::cerr << msg << std::endl;
    return;
  }
}

// Ouvre (ou réouvre) la caméra virtuelle
void Core::openVirtualCamera() {
  if (!videoOutput_->open(profilesManager_.getVirtualCameraSelection(),
                          profilesManager_.getTargetWidth(),
                          profilesManager_.getTargetHeight(), fps)) {
    const std::string msg =
        "Erreur : impossible d'ouvrir la caméra virtuelle "
        "(/dev/video" +
        std::to_string(profilesManager_.getVirtualCameraSelection()) +
        "). Chargez le module avec : sudo modprobe v4l2loopback";
    if (ui_)
      ui_->showMessage(IUi::WARNING, msg);
    std::cerr << msg << std::endl;
    return;
  }

  if (profilesManager_.getTargetWidth() != videoOutput_->getWidth() ||
      profilesManager_.getTargetHeight() != videoOutput_->getHeight()) {
    profilesManager_.setTargetWidth(videoOutput_->getWidth());
    profilesManager_.setTargetHeight(videoOutput_->getHeight());
  }
}

Core::~Core() { stop(); }

void Core::start() {
  if (!init())
    return;

  // Ouvrir la caméra de capture selon la sélection dans ProfileManager
  openCaptureCamera();

  // Ouvrir la caméra virtuelle
  openVirtualCamera();
  // Démarrer la boucle de capture dans un thread séparé
  capturingRunning_ = true;
  captureThread_ = std::thread(&Core::captureLoop, this);
  if (ui_) {
    ui_->run();
  } else {
    std::cout << "No Ui defined, running in console.." << std::endl;
    captureThread_.join();
  }
}

void Core::setUi(IUi *ui) { ui_ = ui; }

ProfileManager &Core::getProfileManager() { return profilesManager_; }

void Core::stop() {
  capturingRunning_ = false;
  if (captureThread_.joinable())
    captureThread_.join();
  if (cap_.isOpened())
    cap_.release();
  if (videoOutput_)
    videoOutput_->stop();
}

void Core::captureLoop() {
  while (capturingRunning_) {
    if (cap_.isOpened()) {
      cv::Mat frame;
      if (cap_.read(frame)) {
        if (processFrame(frame) && ui_) {
          ui_->updateFrame(frame);
        }
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

// Fonction de lissage d'une valeur (pour la stabilité du suivi)
template <typename T>
T smoothValue(const T &current, const T &last, double factor) {
  return factor * current + (1 - factor) * last;
}

bool Core::processFrame(cv::Mat &frame) {
  std::vector<cv::Rect> faces;
  cv::Mat processedOutput;
  int current_model = profilesManager_.getModelSelection();

#if defined(SAF_HAS_OPENCV_CUDA) && SAF_HAS_OPENCV_CUDA
  cv::cuda::GpuMat gpu_frame, gpu_cropped, gpu_resized, gpu_processed;
  gpu_frame.upload(frame);
#endif

  if (current_model == 0) {
    // Détection par Haar Cascade (CPU)
    cv::Mat processedBGRA;
    cv::cvtColor(frame, processedBGRA, cv::COLOR_BGR2BGRA);
    faceCascade_.detectMultiScale(processedBGRA, faces, 1.1, 3, 0,
                                  cv::Size(100, 100));
  } else if (current_model == 1 && !faceNet_.empty()) {
    // Détection par DNN (CPU ou CUDA selon SAF_HAS_OPENCV_CUDA).
    // Wrap in try/catch: on a broken CUDA setup (version mismatch, OOM,
    // cuDNN assert) we don't want to take the whole app down.
    try {
      cv::Mat blob =
          cv::dnn::blobFromImage(frame, 1.0, cv::Size(300, 300),
                                 cv::Scalar(104.0, 177.0, 123.0), false, false);
      faceNet_.setInput(blob);
      cv::Mat detections = faceNet_.forward();
      float conf_threshold = profilesManager_.getDetectionConfidence();

      cv::Mat detectionMat(detections.size[2], detections.size[3], CV_32F,
                           detections.ptr<float>());
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
    } catch (const cv::Exception &e) {
      static bool warned = false;
      if (!warned) {
        warned = true;
        std::cerr << "[core] DNN inference failed: " << e.what() << std::endl;
        std::cerr << "[core] Falling back to Haar Cascade for this run."
                  << std::endl;
      }
      // Silently degrade to no-detection for this frame; retry next frame.
    } catch (const std::exception &e) {
      static bool warned = false;
      if (!warned) {
        warned = true;
        std::cerr << "[core] DNN inference threw: " << e.what() << std::endl;
      }
    }
  }

  if (!faces.empty()) {
    cv::Rect face = faces[0];
    cv::Point2f current_center(
        (face.x + face.width / 2.0f) / static_cast<float>(frame.cols),
        (face.y + face.height / 2.0f) / static_cast<float>(frame.rows));
    cv::Point2f old_center = lastCenter_.load();
    old_center.x = smoothValue(current_center.x, old_center.x,
                               profilesManager_.getSmoothingFactor());
    old_center.y = smoothValue(current_center.y, old_center.y,
                               profilesManager_.getSmoothingFactor());
    lastCenter_.store(old_center);

    double face_size = std::max(face.width, face.height);
    double current_zoom = profilesManager_.getZoomBase() +
                          (face_size / static_cast<double>(frame.cols)) *
                              profilesManager_.getZoomMultiplier();
    double old_zoom = lastZoom_.load();
    lastZoom_.store(smoothValue(current_zoom, old_zoom,
                                profilesManager_.getSmoothingFactor()));
  }

  // Calcul de la région de recadrage
  int crop_width = static_cast<int>(frame.cols / lastZoom_.load());
  int crop_height = static_cast<int>(frame.rows / lastZoom_.load());
  int center_x = static_cast<int>(lastCenter_.load().x * frame.cols);
  int center_y = static_cast<int>(lastCenter_.load().y * frame.rows);
  int x1 = std::max(center_x - crop_width / 2, 0);
  int y1 = std::max(center_y - crop_height / 2, 0);
  int x2 = std::min(center_x + crop_width / 2, frame.cols);
  int y2 = std::min(center_y + crop_height / 2, frame.rows);
  cv::Rect crop_region(x1, y1, x2 - x1, y2 - y1);

  // Guard against degenerate crop regions (avoids OpenCV assertion crashes).
  if (crop_region.width <= 0 || crop_region.height <= 0)
    return false;

  const cv::Size target{profilesManager_.getTargetWidth(),
                        profilesManager_.getTargetHeight()};

#if defined(SAF_HAS_OPENCV_CUDA) && SAF_HAS_OPENCV_CUDA
  if (current_model == 1) {
    gpu_cropped = gpu_frame(crop_region);
    cv::cuda::resize(gpu_cropped, gpu_resized, target);
    cv::cuda::cvtColor(gpu_resized, gpu_processed, cv::COLOR_BGR2BGRA);
    gpu_processed.download(processedOutput);
  } else
#endif
  {
    // CPU path (used for Haar and for DNN when CUDA is not available).
    cv::Mat cropped_frame = frame(crop_region);
    cv::Mat resized;
    cv::resize(cropped_frame, resized, target);
    cv::cvtColor(resized, processedOutput, cv::COLOR_BGR2BGRA);
  }

  if (processedOutput.empty())
    return false;

  processedOutput.copyTo(frame);
  if (videoOutput_)
    return videoOutput_->write(processedOutput);

  return false;
}
