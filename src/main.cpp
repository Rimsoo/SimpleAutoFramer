#include "Core.h"
#include "UiFactory.h"
#include <config.h>
#include <cstdlib>
#include <opencv2/dnn.hpp>
#include <opencv2/opencv.hpp>
#include <sys/stat.h>

int main(int argc, char *argv[]) {

  Core core;
  UiFactory::createUi(core);

  core.start();

  return 0;
}
