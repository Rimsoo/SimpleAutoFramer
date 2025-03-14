// CameraFactory.h
#include "ICamera.h"
#ifdef IS_LINUX
#include "v4l2/LinuxCamera.h"
#elif IS_WINDOWS
#include "WindowsCamera.h"
#endif

class CameraFactory {
public:
  static ICamera *createCamera() {
#ifdef IS_LINUX
    return new LinuxCamera();
#elif IS_WINDOWS
    return new WindowsCamera();
#endif
  }
};
