#include "ProfileManager.h"
#include "Paths.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

ProfileManager::ProfileManager() {
#ifdef INSTALL_DATA_DIR
  profileFilename =
      (saf::userConfigDir() / "simpleautoframer.config.json").string();
#else
  // Dev builds keep their config next to the binary so they don't clobber
  // the installed app's settings.
  fs::path dir = saf::executableDir();
  if (dir.empty())
    dir = fs::current_path();
  profileFilename = (dir / "simpleautoframer.config.json").string();
#endif

  loadProfilesFile();
}

void ProfileManager::setShowQuitMessage(bool value) {
  std::lock_guard<std::mutex> lock(mutex_);
  showQuitMessage = value;
  internalSave();
}

bool ProfileManager::isShowQuitMessage() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return showQuitMessage;
}

void ProfileManager::setHyprlandSync(bool value) {
  std::lock_guard<std::mutex> lock(mutex_);
  hyprlandSync = value;
  internalSave();
}

bool ProfileManager::isHyprlandSync() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return hyprlandSync;
}

// Conversion Profile <-> JSON
nlohmann::json ProfileManager::Profile::toJson() const {
  return {{"camera_selection", camera_selection},
          {"smoothing_factor", smoothing_factor},
          {"detection_confidence", detection_confidence},
          {"model_selection", model_selection},
          {"zoom_base", zoom_base},
          {"zoom_multiplier", zoom_multiplier},
          {"target_width", target_width},
          {"target_height", target_height},
          {"virtual_camera_selection", virtual_camera_selection},
          {"shortcut", shortcut}};
}

std::unique_ptr<ProfileManager::Profile>
ProfileManager::Profile::fromJson(const std::string &name,
                                  const nlohmann::json &j) {
  auto cfg = std::make_unique<Profile>();
  cfg->name = name;
  cfg->camera_selection = j.value("camera_selection", cfg->camera_selection);
  cfg->smoothing_factor = j.value("smoothing_factor", cfg->smoothing_factor);
  cfg->detection_confidence =
      j.value("detection_confidence", cfg->detection_confidence);
  cfg->model_selection = j.value("model_selection", cfg->model_selection);
  cfg->zoom_base = j.value("zoom_base", cfg->zoom_base);
  cfg->zoom_multiplier = j.value("zoom_multiplier", cfg->zoom_multiplier);
  cfg->target_width = j.value("target_width", cfg->target_width);
  cfg->target_height = j.value("target_height", cfg->target_height);
  cfg->virtual_camera_selection =
      j.value("virtual_camera_selection", cfg->virtual_camera_selection);
  cfg->shortcut = j.value("shortcut", cfg->shortcut);
  cfg->sanitize();
  return cfg;
}

void ProfileManager::Profile::sanitize() {
  camera_selection = std::max(camera_selection, 0);
  virtual_camera_selection = std::max(virtual_camera_selection, 0);
  smoothing_factor = std::clamp(smoothing_factor, 0.0, 1.0);
  detection_confidence = std::clamp(detection_confidence, 0.0, 1.0);
  model_selection = std::clamp(model_selection, 0, 1);
  zoom_base = std::clamp(zoom_base, 1.0, 5.0);
  zoom_multiplier = std::clamp(zoom_multiplier, 0.0, 10.0);
  // The virtual camera is fed I420 frames, which need even dimensions.
  target_width = std::clamp(target_width, 160, 7680) & ~1;
  target_height = std::clamp(target_height, 120, 4320) & ~1;
}

std::vector<ProfileManager::Profile> ProfileManager::getProfileList() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Profile> profiles;
  for (const auto &profile : savedProfiles) {
    profiles.push_back(*profile);
  }
  return profiles;
}

std::string ProfileManager::getCurrentProfileName() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return currentProfile->name;
}

bool ProfileManager::profileExists(const std::string &name) {
  std::lock_guard<std::mutex> lock(mutex_);
  return findProfile(name) != savedProfiles.end();
}

// Gestion des profils
void ProfileManager::createProfile(const std::string &name) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto conf = findProfile(name);
  if (conf != savedProfiles.end()) {
    currentProfile = conf->get();
    return;
  }

  create(name);
}

void ProfileManager::create(const std::string &name = "default") {
  auto newProfile = currentProfile != nullptr
                        ? std::make_unique<Profile>(*currentProfile)
                        : std::make_unique<Profile>();
  newProfile->name = name;
  // A copied shortcut would make two profiles fight over the same keys.
  newProfile->shortcut.clear();
  currentProfile = newProfile.get();
  savedProfiles.push_back(std::move(newProfile));
  internalSave();
}

void ProfileManager::deleteProfile(const std::string &name) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = findProfile(name);
  if (it == savedProfiles.end())
    return;

  const bool wasCurrent = it->get() == currentProfile;
  savedProfiles.erase(it);

  if (savedProfiles.empty()) {
    currentProfile = nullptr;
    create();
    return;
  }
  if (wasCurrent)
    currentProfile = savedProfiles.front().get();
  internalSave();
}

void ProfileManager::switchProfile(const std::string &name) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = findProfile(name);
  if (it != savedProfiles.end()) {
    currentProfile = it->get();
    internalSave();
  }
}

// Chargement/Sauvegarde
void ProfileManager::loadProfilesFile() {
  std::lock_guard<std::mutex> lock(mutex_);
  savedProfiles.clear();
  currentProfile = nullptr;

  if (!std::filesystem::exists(profileFilename)) {
    create();
    return;
  }

  nlohmann::json j;
  try {
    std::ifstream inFile(profileFilename);
    if (!inFile) {
      std::cerr << "Avertissement : impossible d'ouvrir " << profileFilename
                << " ; utilisation des valeurs par défaut." << std::endl;
      create();
      return;
    }
    inFile >> j;
  } catch (const nlohmann::json::exception &ex) {
    std::cerr << "Avertissement : configuration JSON corrompue ("
              << profileFilename << ") : " << ex.what()
              << ". Un profil par défaut est créé." << std::endl;
    create();
    return;
  }

  try {
    if (j.contains("profiles") && j["profiles"].is_object()) {
      for (const auto &[key, value] : j["profiles"].items()) {
        if (value.is_object())
          savedProfiles.push_back(Profile::fromJson(key, value));
      }
    }
    showQuitMessage = j.value("show_quit_message", true);
    hyprlandSync = j.value("hyprland_sync", true);
  } catch (const nlohmann::json::exception &ex) {
    std::cerr << "Avertissement : certains profils n'ont pu être chargés : "
              << ex.what() << std::endl;
  }

  std::string lastUsed = "default";
  if (j.contains("last_used") && j["last_used"].is_string())
    lastUsed = j["last_used"].get<std::string>();
  auto it = findProfile(lastUsed);
  if (it != savedProfiles.end()) {
    currentProfile = it->get();
  } else if (!savedProfiles.empty()) {
    currentProfile = savedProfiles.front().get();
  } else {
    create();
  }
}

void ProfileManager::saveProfileFiles() {
  std::lock_guard<std::mutex> lock(mutex_);
  internalSave();
}

// Helpers privés
void ProfileManager::internalSave() {
  if (!currentProfile)
    return;

  nlohmann::json j;
  j["show_quit_message"] = showQuitMessage;
  j["hyprland_sync"] = hyprlandSync;
  j["last_used"] = currentProfile->name;

  for (const auto &profile : savedProfiles) {
    j["profiles"][profile->name] = profile->toJson();
  }

  // Atomic write: dump to a sibling .tmp file then rename().
  // Protects the config from partial writes on crash / power loss.
  const std::string tmp = profileFilename + ".tmp";
  try {
    {
      std::ofstream outFile(tmp, std::ios::binary | std::ios::trunc);
      if (!outFile) {
        std::cerr << "Avertissement : impossible de créer " << tmp << " ("
                  << std::strerror(errno) << ")" << std::endl;
        return;
      }
      outFile << j.dump(4);
      outFile.flush();
      if (!outFile) {
        std::cerr << "Avertissement : écriture partielle dans " << tmp
                  << std::endl;
        std::remove(tmp.c_str());
        return;
      }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, profileFilename, ec);
    if (ec) {
      std::cerr << "Avertissement : échec du renommage de " << tmp << " vers "
                << profileFilename << " : " << ec.message() << std::endl;
      std::remove(tmp.c_str());
    }
  } catch (const std::exception &e) {
    std::cerr << "Avertissement : exception lors de l'écriture du config : "
              << e.what() << std::endl;
    std::remove(tmp.c_str());
  }
}

std::vector<std::unique_ptr<ProfileManager::Profile>>::iterator
ProfileManager::findProfile(const std::string &name) {
  return std::find_if(
      savedProfiles.begin(), savedProfiles.end(),
      [&name](const std::unique_ptr<Profile> &c) { return c->name == name; });
}

// Getters avec verrouillage -------------------------------
#define SAF_GETTER(type, fn, field)                                            \
  type ProfileManager::fn() const {                                            \
    std::lock_guard<std::mutex> lock(mutex_);                                  \
    return currentProfile->field;                                              \
  }

SAF_GETTER(int, getCameraSelection, camera_selection)
SAF_GETTER(double, getSmoothingFactor, smoothing_factor)
SAF_GETTER(double, getDetectionConfidence, detection_confidence)
SAF_GETTER(int, getModelSelection, model_selection)
SAF_GETTER(double, getZoomBase, zoom_base)
SAF_GETTER(double, getZoomMultiplier, zoom_multiplier)
SAF_GETTER(int, getTargetWidth, target_width)
SAF_GETTER(int, getTargetHeight, target_height)
SAF_GETTER(int, getVirtualCameraSelection, virtual_camera_selection)
SAF_GETTER(std::string, getShortcut, shortcut)
#undef SAF_GETTER

// Setters avec verrouillage -------------------------------
#define SAF_SETTER(type, fn, field)                                            \
  void ProfileManager::fn(type value) {                                        \
    std::lock_guard<std::mutex> lock(mutex_);                                  \
    currentProfile->field = value;                                             \
    currentProfile->sanitize();                                                \
  }

SAF_SETTER(int, setCameraSelection, camera_selection)
SAF_SETTER(double, setSmoothingFactor, smoothing_factor)
SAF_SETTER(double, setDetectionConfidence, detection_confidence)
SAF_SETTER(int, setModelSelection, model_selection)
SAF_SETTER(double, setZoomBase, zoom_base)
SAF_SETTER(double, setZoomMultiplier, zoom_multiplier)
SAF_SETTER(int, setTargetWidth, target_width)
SAF_SETTER(int, setTargetHeight, target_height)
SAF_SETTER(int, setVirtualCameraSelection, virtual_camera_selection)
SAF_SETTER(const std::string &, setShortcut, shortcut)
#undef SAF_SETTER
