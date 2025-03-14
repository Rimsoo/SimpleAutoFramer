// LinuxCamera.cpp
#include "LinuxCamera.h"
#include <filesystem>
#include <iostream>
#include <sys/stat.h>

LinuxCamera::LinuxCamera() : v4l2Output(nullptr) {}
LinuxCamera::~LinuxCamera() { stop(); }

namespace fs = std::filesystem;
bool LinuxCamera::open(int index, int width, int height, int fps) {
  std::string outputFile = "/dev/video" + std::to_string(index);
  if (!fs::exists(outputFile)) {
    auto msg = "Attention : " + std::string(outputFile) +
               " n'existe pas. Vérifiez v4l2loopback.";
    // if (ui_)
    //   ui_->showMessage(IUi::WARNING, msg);

    std::cerr << msg << std::endl;

    return false;
  }

  stop();

  V4L2DeviceParameters paramOut(outputFile.c_str(),
                                v4l2_fourcc('B', 'G', 'R', '4'), width, height,
                                fps, IOTYPE_MMAP);
  v4l2Output = V4l2Output::create(paramOut);

  if (!v4l2Output) {
    auto msg = "Erreur : Impossible d'ouvrir la sortie " + outputFile;

    // if (ui_)
    //   ui_->showMessage(IUi::ERROR, msg);

    std::cerr << msg << std::endl;
    return false;
  }

  return v4l2Output != nullptr;
}

void LinuxCamera::stop() {
  if (v4l2Output) {
    v4l2Output->stop();
    delete v4l2Output;
    v4l2Output = nullptr;
  }
  // if (physicalCamera.isOpened()) {
  //   physicalCamera.release();
  // }
}

bool LinuxCamera::write(const cv::Mat &frame) {
  if (!v4l2Output)
    return false;

  size_t bufferSize = frame.total() * frame.elemSize();
  char *buffer = reinterpret_cast<char *>(frame.data);
  size_t nb = v4l2Output->write(buffer, bufferSize);

  return nb == bufferSize;
}

bool LinuxCamera::isOpened() const { return v4l2Output != nullptr; }

int LinuxCamera::getWidth() const {
  if (v4l2Output)
    return v4l2Output->getWidth();
  return 0;
}

int LinuxCamera::getHeight() const {
  if (v4l2Output)
    return v4l2Output->getHeight();
  return 0;
}
