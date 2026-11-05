// Core.h
#pragma once
#include "CameraFactory.h"
#include "ICamera.h"
#include "IUi.h"
#include "ProfileManager.h"

#include <atomic>
#include <config.h>
#include <memory>
#include <mutex>
#include <opencv2/core/mat.hpp>
#include <opencv2/opencv.hpp>
// OpenCV 5 moved the classic Haar/LBP cv::CascadeClassifier out of the core
// objdetect module into the contrib "xobjdetect" module. On OpenCV 4 it is
// still provided transitively via <opencv2/opencv.hpp>.
#if defined(CV_VERSION_MAJOR) && CV_VERSION_MAJOR >= 5
#include <opencv2/xobjdetect.hpp>
#endif
#include <thread>

#if defined(SAF_HAS_OPENCV_CUDA) && SAF_HAS_OPENCV_CUDA
#include <opencv2/cudaarithm.hpp>
#include <opencv2/cudaimgproc.hpp>
#include <opencv2/cudawarping.hpp>
#endif

class Core {
public:
  explicit Core();
  ~Core();

  // Démarre la capture puis la boucle UI (bloquant jusqu'à la fermeture)
  void start();

  // Arrête le traitement et libère les ressources
  void stop();

  void openCaptureCamera();

  void openVirtualCamera();

  // Réouvre les caméras si le profil courant en désigne d'autres que celles
  // actuellement ouvertes (changement de profil, raccourci…).
  void reloadDevicesIfNeeded();

  void setUi(IUi *ui);

  ProfileManager &getProfileManager();

private:
  // Boucle de capture, exécutée dans un thread
  void captureLoop();
  // Détecte, recadre et envoie la frame à la caméra virtuelle. En sortie,
  // `frame` contient l'image recadrée (BGR) pour la prévisualisation.
  bool processFrame(cv::Mat &frame);
  void detectFaces(const cv::Mat &frame, std::vector<cv::Rect> &faces,
                   bool use_dnn);

  bool init();

  IUi *ui_;

  ProfileManager profilesManager_;
  std::atomic<bool> capturingRunning_;
  std::thread captureThread_;

  // Protège cap_ et videoOutput_ : le thread de capture les utilise pendant
  // que l'UI peut les réouvrir.
  std::mutex deviceMutex_;
  cv::VideoCapture cap_;
  int openedCamera_ = -1;
  std::unique_ptr<ICamera> videoOutput_;
  int openedVirtualCamera_ = -1;
  int openedWidth_ = 0;
  int openedHeight_ = 0;

  // Modèles de détection et données de suivi
  bool initialized_ = false;
  cv::CascadeClassifier faceCascade_;
  cv::dnn::Net faceNet_;
  cv::Point2f lastCenter_{0.5f, 0.5f};
  double lastZoom_ = 1.5;

  double fps = 30.0;
};
