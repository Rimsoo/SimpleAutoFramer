// GtkUserInterface.h
#pragma once
#include "Core.h"
#include "IUi.h"
#include "MainWindow.h"
#include <gtkmm/application.h>

class GtkUi : public IUi {
public:
  GtkUi(Core &core);
  virtual ~GtkUi() = default;

  void showMessage(MessageType type, const std::string &message) override;
  void updateFrame(const cv::Mat &frame) override;
  int run() override;

private:
  MainWindow *mainWindow_;
  Glib::RefPtr<Gtk::Application> app_;
};
