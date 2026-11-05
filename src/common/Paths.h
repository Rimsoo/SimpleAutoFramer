// Paths.h — locate bundled resources and the per-user config directory.
#pragma once

#include <config.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef __linux__
#include <unistd.h>
#endif

namespace saf {

namespace fs = std::filesystem;

// Directory containing the running executable (empty if it can't be found).
inline fs::path executableDir() {
#ifdef __linux__
  std::error_code ec;
  fs::path exe = fs::read_symlink("/proc/self/exe", ec);
  if (!ec)
    return exe.parent_path();
#endif
  return {};
}

// Resolve a data file (glade, css, cascade, icon…). Search order:
//   1. $SIMPLEAUTOFRAMER_DATA_DIR
//   2. the executable's directory (dev builds copy data/ next to the binary)
//   3. <exe>/../share/simpleautoframer (relocatable installs, AppImage)
//   4. the install data dir (INSTALL_MODE builds)
//   5. the current working directory
// Returns the first existing candidate, or the last one tried if none exist
// so error messages still show a meaningful path.
inline std::string resourcePath(const std::string &name) {
  std::vector<fs::path> dirs;
  if (const char *env = std::getenv("SIMPLEAUTOFRAMER_DATA_DIR"); env && *env)
    dirs.emplace_back(env);
  if (auto exeDir = executableDir(); !exeDir.empty()) {
    dirs.push_back(exeDir);
    dirs.push_back(exeDir.parent_path() / "share" / "simpleautoframer");
  }
#ifdef INSTALL_DATA_DIR
  dirs.emplace_back(INSTALL_DATA_DIR);
#endif
  std::error_code ec;
  dirs.push_back(fs::current_path(ec));

  fs::path candidate;
  for (const auto &dir : dirs) {
    candidate = dir / name;
    if (fs::exists(candidate, ec))
      return candidate.string();
  }
  return candidate.string();
}

// Per-user config directory: $XDG_CONFIG_HOME/simpleautoframer, falling back
// to ~/.config/simpleautoframer. Created on demand.
inline fs::path userConfigDir() {
  fs::path base;
  if (const char *xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
    base = xdg;
  else if (const char *home = std::getenv("HOME"); home && *home)
    base = fs::path(home) / ".config";
  else
    base = fs::temp_directory_path();
  fs::path dir = base / "simpleautoframer";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

// When running as an AppImage, install a desktop entry pointing at it.
// xdg-desktop-portal only accepts our app id (global shortcuts) when
// SAF_APP_ID.desktop exists, and it gives the app a launcher entry.
// Never overwrites an entry installed by a package.
inline void integrateAppImage() {
  const char *appimage = std::getenv("APPIMAGE");
  if (!appimage || !*appimage)
    return;

  const std::string desktopName = std::string(SAF_APP_ID) + ".desktop";
  std::error_code ec;
  for (const char *sys : {"/usr/share/applications/",
                          "/usr/local/share/applications/"})
    if (fs::exists(std::string(sys) + desktopName, ec))
      return;

  fs::path dataHome;
  if (const char *xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
    dataHome = xdg;
  else if (const char *home = std::getenv("HOME"); home && *home)
    dataHome = fs::path(home) / ".local" / "share";
  else
    return;

  const fs::path desktop = dataHome / "applications" / desktopName;
  const fs::path icon =
      dataHome / "icons" / "hicolor" / "64x64" / "apps" / "simpleautoframer.png";
  const std::string exec = std::string("Exec=\"") + appimage + "\"";

  // Up to date already (same AppImage path)?
  {
    std::ifstream in(desktop);
    std::string line;
    while (std::getline(in, line))
      if (line == exec && fs::exists(icon, ec))
        return;
  }

  fs::create_directories(desktop.parent_path(), ec);
  fs::create_directories(icon.parent_path(), ec);
  fs::copy_file(resourcePath("simpleautoframer.png"), icon,
                fs::copy_options::overwrite_existing, ec);
  std::ofstream out(desktop, std::ios::trunc);
  out << "[Desktop Entry]\n"
         "Name=SimpleAutoFramer\n"
         "Comment=Automatic webcam framing\n"
      << exec << "\n"
      << "Icon=simpleautoframer\n"
         "Terminal=false\n"
         "Type=Application\n"
         "Categories=AudioVideo;Video;GTK;\n"
         "X-AppImage-Integrate=false\n";
}

} // namespace saf
