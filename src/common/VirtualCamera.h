// VirtualCamera.h
#pragma once
#include <opencv2/core/mat.hpp>

class VirtualCamera {
public:
  virtual ~VirtualCamera() = default;
  virtual size_t write(const cv::Mat &frame) = 0;
  virtual bool isOpened() const = 0;
  virtual void stop() = 0;
};
