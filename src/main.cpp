#include "Core.h"
#include "GtkUi.h"
#include <config.h>
#include <cstdlib>
#include <opencv2/dnn.hpp>
#include <opencv2/opencv.hpp>
#include <sys/stat.h>

int main(int argc, char *argv[]) {

  Core core;

  GtkUi ui(core);

  core.start();

  return 0;
}
