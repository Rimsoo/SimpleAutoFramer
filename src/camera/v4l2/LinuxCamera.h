// LinuxCamera.h
#include "ICamera.h"
#include "V4l2Output.h"
#include <opencv2/opencv.hpp>

class LinuxCamera : public ICamera {
public:
  LinuxCamera();
  virtual ~LinuxCamera();

  bool open(int index, int width, int height, int fps) override;
  // bool open(const std::string &output) override;
  void stop() override;
  bool write(const cv::Mat &frame) override;
  int getWidth() const override;
  int getHeight() const override;
  bool isOpened() const override;

private:
  V4l2Output *v4l2Output;
  // cv::VideoCapture physicalCamera;
};
