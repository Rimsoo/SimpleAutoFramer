// Core.cpp
#include "Core.h"
#include "Paths.h"
#include <algorithm>
#include <chrono>
#include <config.h>
#include <iostream>
#include <opencv2/core/cuda.hpp>

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

  // YuNet (ONNX) face detector: far more accurate than Haar and fast even on
  // CPU. Optional: without it we fall back to the Haar Cascade.
  yunetModelPath_ = saf::resourcePath("face_detection_yunet_2023mar.onnx");
  if (!createYuNet(cv::cuda::getCudaEnabledDeviceCount() > 0) &&
      !createYuNet(false))
    std::cerr << "[core] YuNet indisponible, utilisation du Haar Cascade."
              << std::endl;

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
    if (error.empty() && cap_.isOpened()) {
      // OpenCV defaults to 640x480 YUYV; cropping into that and scaling up
      // looks blurry. Ask for 1080p MJPG (USB bandwidth allows 30 fps); the
      // driver falls back to the closest supported mode.
      cap_.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
      cap_.set(cv::CAP_PROP_FRAME_WIDTH, 1920);
      cap_.set(cv::CAP_PROP_FRAME_HEIGHT, 1080);
      cap_.set(cv::CAP_PROP_FPS, 30);
      std::cerr << "[core] Caméra /dev/video" << index << " : "
                << cap_.get(cv::CAP_PROP_FRAME_WIDTH) << "x"
                << cap_.get(cv::CAP_PROP_FRAME_HEIGHT) << " @ "
                << cap_.get(cv::CAP_PROP_FPS) << " fps" << std::endl;
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

bool Core::createYuNet(bool gpu) {
  try {
    const int backend = gpu ? cv::dnn::DNN_BACKEND_CUDA
                            : cv::dnn::DNN_BACKEND_OPENCV;
    const int target = gpu ? cv::dnn::DNN_TARGET_CUDA : cv::dnn::DNN_TARGET_CPU;
    faceDetector_ = cv::FaceDetectorYN::create(
        yunetModelPath_, "", cv::Size(320, 320), 0.6f, 0.3f, 50, backend,
        target);
  } catch (const cv::Exception &ex) {
    std::cerr << "[core] Chargement de YuNet (" << (gpu ? "GPU" : "CPU")
              << ") impossible : " << ex.what() << std::endl;
    faceDetector_.reset();
    return false;
  }
  yunetOnGpu_ = gpu;
  std::cerr << "[core] Détecteur YuNet chargé (" << (gpu ? "GPU CUDA" : "CPU")
            << ")." << std::endl;
  return true;
}

void Core::detectFacesYuNet(const cv::Mat &frame,
                            std::vector<cv::Rect> &faces) {
  // Detect on a ~640 px wide copy: plenty for a face filling a webcam frame,
  // and several times cheaper than full resolution.
  const double scale = frame.cols > 640 ? 640.0 / frame.cols : 1.0;
  cv::Mat small;
  if (scale < 1.0)
    cv::resize(frame, small, cv::Size(), scale, scale, cv::INTER_AREA);
  else
    small = frame;

  cv::Mat detections;
  try {
    if (faceDetector_->getInputSize() != small.size())
      faceDetector_->setInputSize(small.size());
    faceDetector_->setScoreThreshold(
        static_cast<float>(profilesManager_.getDetectionConfidence()));
    faceDetector_->detect(small, detections);
  } catch (const cv::Exception &ex) {
    // Typically a CUDA/cuDNN mismatch, only visible at the first inference.
    std::cerr << "[core] YuNet a échoué" << (yunetOnGpu_ ? " sur GPU" : "")
              << " : " << ex.what() << std::endl;
    if (!yunetOnGpu_ || !createYuNet(false))
      faceDetector_.reset();
    return;
  }

  // Each row: x, y, w, h, 5 landmarks (x, y), score.
  const cv::Rect bounds(0, 0, frame.cols, frame.rows);
  for (int i = 0; i < detections.rows; ++i) {
    const float *d = detections.ptr<float>(i);
    cv::Rect r(cvRound(d[0] / scale), cvRound(d[1] / scale),
               cvRound(d[2] / scale), cvRound(d[3] / scale));
    r &= bounds;
    if (r.area() > 0)
      faces.push_back(r);
  }
}

void Core::detectFaces(const cv::Mat &frame, std::vector<cv::Rect> &faces,
                       bool use_yunet) {
  if (use_yunet) {
    detectFacesYuNet(frame, faces);
    return;
  }

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
}

bool Core::processFrame(cv::Mat &frame) {
  const bool use_yunet =
      profilesManager_.getModelSelection() == 1 && faceDetector_;

  std::vector<cv::Rect> faces;
  detectFaces(frame, faces, use_yunet);

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
  cv::resize(frame(crop_region), output, target, 0, 0, cv::INTER_LINEAR);
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
