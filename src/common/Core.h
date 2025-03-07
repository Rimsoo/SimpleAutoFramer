// Core.h
#pragma once
#include "IUi.h"
#include "ProfileManager.h"
#include "V4l2Output.h"
#include <atomic>
#include <mutex>
#include <opencv2/core/mat.hpp>
#include <opencv2/opencv.hpp>
#include <thread>

class Core {
public:
  explicit Core();
  ~Core();

  // Démarre la capture et le traitement dans un thread séparé
  void start();

  // Arrête le traitement et libère les ressources
  void stop();

  void openCaptureCamera();

  void openVirtualCamera();

  void setUi(IUi *ui);

  ProfileManager &getProfileManager();

private:
  // Boucle de capture, exécutée dans un thread
  void captureLoop();
  // Cette méthode peut être appelée périodiquement pour traiter la dernière
  // frame capturée
  bool processFrame(cv::Mat &frame);

  bool init();

  IUi *ui_;

  ProfileManager profilesManager_;
  std::atomic<bool> capturingRunning_;
  std::thread captureThread_;
  std::mutex frameMutex_;
  cv::VideoCapture cap_;
  V4l2Output *videoOutput_;

  // Modèles de détection et données de suivi
  cv::CascadeClassifier faceCascade_;
  cv::dnn::Net faceNet_;
  std::atomic<cv::Point2f> lastCenter_;
  std::atomic<double> lastZoom_;

  double fps = 30.0;
};
