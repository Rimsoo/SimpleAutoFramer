// Core.cpp
#include "Core.h"
#include "Paths.h"
#include <algorithm>
#include <chrono>
#include <config.h>
#include <iostream>

Core::Core()
    : ui_(nullptr), capturingRunning_(false),
      videoOutput_(CameraFactory::createCamera()) {}

bool Core::init() {
  if (initialized_)
    return true;

  const std::string cascade_path =
      saf::resourcePath("haarcascade_frontalface_default.xml");

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

#if defined(CV_VERSION_MAJOR) && CV_VERSION_MAJOR >= 5
  // OpenCV 5 removed the Caffe importer from the dnn module, so the bundled
  // res10 SSD Caffe model can no longer be loaded. The DNN detection path is
  // optional: leaving faceNet_ empty makes processFrame() fall back to the
  // Haar Cascade detector, so face tracking keeps working.
  std::cerr << "[core] OpenCV 5 : modèle DNN Caffe non supporté, "
               "utilisation du Haar Cascade."
            << std::endl;
#else
  // Chargement du modèle DNN res10_300x300_ssd_iter_140000
  const std::string dnn_proto_path = saf::resourcePath("deploy.prototxt");
  const std::string dnn_model_path =
      saf::resourcePath("res10_300x300_ssd_iter_140000.caffemodel");
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
    std::cerr << msg << std::endl;
    // Not a fatal error: Haar Cascade is still available.
  }
#endif

  initialized_ = true;
  return true;
}

// Ouvre (ou réouvre) la caméra de capture
void Core::openCaptureCamera() {
  const int index = profilesManager_.getCameraSelection();
  std::string error;
  {
    std::lock_guard<std::mutex> lock(deviceMutex_);
    if (cap_.isOpened())
      cap_.release();
    openedCamera_ = index;

    try {
#ifdef __linux__
      // Explicit V4L2: OpenCV may otherwise pick GStreamer, which is slower
      // and noisier for plain webcams.
      if (!cap_.open(index, cv::CAP_V4L2))
#endif
        cap_.open(index);
    } catch (const cv::Exception &ex) {
      error = std::string("Erreur OpenCV à l'ouverture de la caméra : ") +
              ex.what();
    }
    if (error.empty() && !cap_.isOpened())
      error = "Erreur : impossible d'ouvrir la caméra /dev/video" +
              std::to_string(index) +
              ".\nSélectionnez une autre caméra dans les paramètres.";
  }

  // Report outside the lock: showMessage runs a modal dialog.
  if (!error.empty()) {
    std::cerr << error << std::endl;
    if (ui_)
      ui_->showMessage(IUi::ERROR, error);
  }
}

// Ouvre (ou réouvre) la caméra virtuelle
void Core::openVirtualCamera() {
  const int index = profilesManager_.getVirtualCameraSelection();
  const int width = profilesManager_.getTargetWidth();
  const int height = profilesManager_.getTargetHeight();
  bool ok;
  int realWidth = 0, realHeight = 0;
  {
    std::lock_guard<std::mutex> lock(deviceMutex_);
    ok = videoOutput_ && videoOutput_->open(index, width, height, fps);
    openedVirtualCamera_ = index;
    openedWidth_ = width;
    openedHeight_ = height;
    if (ok) {
      realWidth = videoOutput_->getWidth();
      realHeight = videoOutput_->getHeight();
    }
  }

  if (!ok) {
    const std::string msg =
        "Impossible d'ouvrir la caméra virtuelle /dev/video" +
        std::to_string(index) +
        ".\nChargez le module : sudo modprobe v4l2loopback video_nr=" +
        std::to_string(index) +
        " card_label=AutoFrameCam exclusive_caps=1\n\n"
        "La prévisualisation reste disponible.";
    std::cerr << msg << std::endl;
    if (ui_)
      ui_->showMessage(IUi::WARNING, msg);
    return;
  }

  // The device may impose its own size (e.g. a reader is already attached).
  if (realWidth > 0 && realHeight > 0 &&
      (realWidth != width || realHeight != height)) {
    profilesManager_.setTargetWidth(realWidth);
    profilesManager_.setTargetHeight(realHeight);
    std::lock_guard<std::mutex> lock(deviceMutex_);
    openedWidth_ = profilesManager_.getTargetWidth();
    openedHeight_ = profilesManager_.getTargetHeight();
  }
}

void Core::reloadDevicesIfNeeded() {
  int camera, vcam, w, h;
  {
    std::lock_guard<std::mutex> lock(deviceMutex_);
    camera = openedCamera_;
    vcam = openedVirtualCamera_;
    w = openedWidth_;
    h = openedHeight_;
  }
  if (camera != profilesManager_.getCameraSelection())
    openCaptureCamera();
  if (vcam != profilesManager_.getVirtualCameraSelection() ||
      w != profilesManager_.getTargetWidth() ||
      h != profilesManager_.getTargetHeight())
    openVirtualCamera();
}

Core::~Core() { stop(); }

void Core::start() {
  if (!init())
    return;

  openCaptureCamera();
  openVirtualCamera();

  capturingRunning_ = true;
  captureThread_ = std::thread(&Core::captureLoop, this);
  if (ui_) {
    ui_->run();
    // The UI loop has exited: tear down before GTK objects go away.
    stop();
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
  std::lock_guard<std::mutex> lock(deviceMutex_);
  if (cap_.isOpened())
    cap_.release();
  if (videoOutput_)
    videoOutput_->stop();
}

void Core::captureLoop() {
  while (capturingRunning_) {
    cv::Mat frame;
    bool got = false;
    {
      std::lock_guard<std::mutex> lock(deviceMutex_);
      got = cap_.isOpened() && cap_.read(frame) && !frame.empty();
    }
    if (got) {
      if (processFrame(frame) && ui_)
        ui_->updateFrame(frame);
      // cap_.read() already paces us at the camera frame rate; just yield so
      // the UI thread can grab deviceMutex_ to reopen devices.
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } else {
      // No camera (or it was unplugged): don't spin.
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
}

// Fonction de lissage d'une valeur (pour la stabilité du suivi)
template <typename T>
T smoothValue(const T &current, const T &last, double factor) {
  return factor * current + (1 - factor) * last;
}

void Core::detectFaces(const cv::Mat &frame, std::vector<cv::Rect> &faces,
                       bool use_dnn) {
  if (!use_dnn) {
    // Haar Cascade (CPU) on a downscaled grayscale image for speed.
    const double scale = frame.cols > 640 ? 640.0 / frame.cols : 1.0;
    cv::Mat gray, small;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::resize(gray, small, cv::Size(), scale, scale, cv::INTER_AREA);
    cv::equalizeHist(small, small);
    const int minSize = std::max(24, static_cast<int>(small.cols * 0.08));
    faceCascade_.detectMultiScale(small, faces, 1.1, 4, 0,
                                  cv::Size(minSize, minSize));
    for (auto &f : faces) {
      f = cv::Rect(cvRound(f.x / scale), cvRound(f.y / scale),
                   cvRound(f.width / scale), cvRound(f.height / scale));
    }
    return;
  }

  // DNN (CPU or CUDA). Wrapped in try/catch: a broken CUDA setup (version
  // mismatch, OOM, cuDNN assert) must not take the whole app down.
  try {
    cv::Mat blob =
        cv::dnn::blobFromImage(frame, 1.0, cv::Size(300, 300),
                               cv::Scalar(104.0, 177.0, 123.0), false, false);
    faceNet_.setInput(blob);
    cv::Mat detections = faceNet_.forward();
    const float conf_threshold =
        static_cast<float>(profilesManager_.getDetectionConfidence());

    cv::Mat detectionMat(detections.size[2], detections.size[3], CV_32F,
                         detections.ptr<float>());
    const cv::Rect bounds(0, 0, frame.cols, frame.rows);
    for (int i = 0; i < detectionMat.rows; i++) {
      float confidence = detectionMat.at<float>(i, 2);
      if (confidence > conf_threshold) {
        int x1 = static_cast<int>(detectionMat.at<float>(i, 3) * frame.cols);
        int y1 = static_cast<int>(detectionMat.at<float>(i, 4) * frame.rows);
        int x2 = static_cast<int>(detectionMat.at<float>(i, 5) * frame.cols);
        int y2 = static_cast<int>(detectionMat.at<float>(i, 6) * frame.rows);
        cv::Rect r = cv::Rect(x1, y1, x2 - x1, y2 - y1) & bounds;
        if (r.area() > 0)
          faces.push_back(r);
      }
    }
  } catch (const std::exception &e) {
    static bool warned = false;
    if (!warned) {
      warned = true;
      std::cerr << "[core] DNN inference failed: " << e.what() << std::endl;
    }
  }
}

bool Core::processFrame(cv::Mat &frame) {
  const bool use_dnn =
      profilesManager_.getModelSelection() == 1 && !faceNet_.empty();

  std::vector<cv::Rect> faces;
  detectFaces(frame, faces, use_dnn);

  const double smoothing = profilesManager_.getSmoothingFactor();
  if (!faces.empty()) {
    // Track the most prominent (largest) face.
    const cv::Rect face = *std::max_element(
        faces.begin(), faces.end(),
        [](const cv::Rect &a, const cv::Rect &b) { return a.area() < b.area(); });
    const cv::Point2f current_center(
        (face.x + face.width / 2.0f) / static_cast<float>(frame.cols),
        (face.y + face.height / 2.0f) / static_cast<float>(frame.rows));
    lastCenter_.x = smoothValue(current_center.x, lastCenter_.x, smoothing);
    lastCenter_.y = smoothValue(current_center.y, lastCenter_.y, smoothing);

    const double face_size = std::max(face.width, face.height);
    const double current_zoom = profilesManager_.getZoomBase() +
                                (face_size / static_cast<double>(frame.cols)) *
                                    profilesManager_.getZoomMultiplier();
    lastZoom_ = smoothValue(current_zoom, lastZoom_, smoothing);
  }
  lastZoom_ = std::max(lastZoom_, 1.0);

  const int target_w = profilesManager_.getTargetWidth();
  const int target_h = profilesManager_.getTargetHeight();
  if (target_w <= 0 || target_h <= 0)
    return false;

  // Crop rectangle with the output aspect ratio, as large as the zoom allows,
  // then slid (not clipped) so it stays inside the frame. Clipping would
  // squash the image whenever the face nears an edge.
  const double aspect = static_cast<double>(target_w) / target_h;
  double crop_w = frame.cols / lastZoom_;
  double crop_h = crop_w / aspect;
  if (crop_h > frame.rows / lastZoom_) {
    crop_h = frame.rows / lastZoom_;
    crop_w = crop_h * aspect;
  }
  const int cw = std::clamp(static_cast<int>(crop_w), 2, frame.cols);
  const int ch = std::clamp(static_cast<int>(crop_h), 2, frame.rows);
  const int cx = static_cast<int>(lastCenter_.x * frame.cols) - cw / 2;
  const int cy = static_cast<int>(lastCenter_.y * frame.rows) - ch / 2;
  const cv::Rect crop_region(std::clamp(cx, 0, frame.cols - cw),
                             std::clamp(cy, 0, frame.rows - ch), cw, ch);

  const cv::Size target{target_w, target_h};
  cv::Mat output;
#if defined(SAF_HAS_OPENCV_CUDA) && SAF_HAS_OPENCV_CUDA
  if (use_dnn) {
    cv::cuda::GpuMat gpu_frame, gpu_resized;
    gpu_frame.upload(frame);
    cv::cuda::resize(gpu_frame(crop_region), gpu_resized, target);
    gpu_resized.download(output);
  } else
#endif
  {
    cv::resize(frame(crop_region), output, target, 0, 0, cv::INTER_LINEAR);
  }
  if (output.empty())
    return false;

  {
    std::lock_guard<std::mutex> lock(deviceMutex_);
    if (videoOutput_ && videoOutput_->isOpened() &&
        videoOutput_->getWidth() == target_w &&
        videoOutput_->getHeight() == target_h)
      videoOutput_->write(output);
  }

  // Preview is shown even when the virtual camera is unavailable.
  frame = output;
  return true;
}
