// CameraInterface.h
#pragma once
#include <opencv2/core/mat.hpp>

class ICamera {
public:
  virtual ~ICamera() = default;

  virtual bool open(int index, int width, int height,
                    int fps) = 0; // Pour caméra physique
  // virtual bool open(const std::string &output) = 0; // Pour caméra virtuelle
  virtual void stop() = 0;
  virtual bool write(const cv::Mat &frame) = 0;
  virtual int getWidth() const = 0;
  virtual int getHeight() const = 0;
  virtual bool isOpened() const = 0;
};
