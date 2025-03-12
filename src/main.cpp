#include "Core.h"
#include <config.h>
#include <cstdlib>
#include <opencv2/dnn.hpp>
#include <opencv2/opencv.hpp>
#include <sys/stat.h>

#ifdef IS_LINUX
#include "GtkUi.h"
#endif

int main(int argc, char *argv[]) {

  Core core;
#ifdef IS_LINUX
  GtkUi ui(core);
#endif

  core.start();

  return 0;
}
