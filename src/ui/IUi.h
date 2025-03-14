// IUserInterface.h
#pragma once
#include <opencv2/core.hpp>
#include <string>

class IUi {
public:
  enum MessageType { INFO, WARNING, ERROR };

  virtual ~IUi() = default;
  virtual void showMessage(MessageType type, const std::string &message) = 0;
  virtual void updateFrame(const cv::Mat &frame) = 0;
  // Démarre la boucle principale de l’interface graphique
  virtual int run() = 0;
};
