// LinuxCamera.cpp
#include "LinuxCamera.h"
#include <filesystem>
#include <iostream>

LinuxCamera::LinuxCamera() : v4l2Output(nullptr) {}
LinuxCamera::~LinuxCamera() { stop(); }

namespace fs = std::filesystem;
bool LinuxCamera::open(int index, int width, int height, int fps) {
  stop();

  std::string outputFile = "/dev/video" + std::to_string(index);
  if (!fs::exists(outputFile)) {
    std::cerr << "Attention : " << outputFile
              << " n'existe pas. Vérifiez v4l2loopback." << std::endl;
    return false;
  }

  // I420 (YU12) is the format every consumer understands: Chromium, Firefox,
  // Zoom, Teams, OBS… BGR32 is rejected by most browsers.
  V4L2DeviceParameters paramOut(outputFile.c_str(),
                                v4l2_fourcc('Y', 'U', '1', '2'), width & ~1,
                                height & ~1, fps, IOTYPE_MMAP);
  v4l2Output = V4l2Output::create(paramOut);

  if (!v4l2Output) {
    std::cerr << "Erreur : impossible d'ouvrir la sortie " << outputFile
              << std::endl;
    return false;
  }
  return true;
}

void LinuxCamera::stop() {
  if (v4l2Output) {
    v4l2Output->stop();
    delete v4l2Output;
    v4l2Output = nullptr;
  }
}

bool LinuxCamera::write(const cv::Mat &frame) {
  if (!v4l2Output || frame.empty())
    return false;

  cv::cvtColor(frame, yuv_, cv::COLOR_BGR2YUV_I420);
  const size_t bufferSize = yuv_.total() * yuv_.elemSize();
  size_t nb =
      v4l2Output->write(reinterpret_cast<char *>(yuv_.data), bufferSize);
  return nb == bufferSize;
}

bool LinuxCamera::isOpened() const { return v4l2Output != nullptr; }

int LinuxCamera::getWidth() const {
  return v4l2Output ? v4l2Output->getWidth() : 0;
}

int LinuxCamera::getHeight() const {
  return v4l2Output ? v4l2Output->getHeight() : 0;
}
