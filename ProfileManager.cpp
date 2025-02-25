#include "ProfileManager.h"
#include "glibmm/ustring.h"

ProfileManager::ProfileManager(const std::string& profileFilePath) 
    : profileFilename(profileFilePath) {
    loadProfilesFile();
}

void ProfileManager::setShowQuitMessage(bool value) {
    showQuitMessage = value;
    internalSave();
}

// Conversion Profile <-> JSON
nlohmann::json ProfileManager::Profile::toJson() const {
    return {
        {"camera_selection", camera_selection},
        {"smoothing_factor", smoothing_factor},
        {"detection_confidence", detection_confidence},
        {"model_selection", model_selection},
        {"zoom_base", zoom_base},
        {"zoom_multiplier", zoom_multiplier},
        {"target_width", target_width},
        {"target_height", target_height},
        {"virtual_camera_selection", virtual_camera_selection}
    };
}

ProfileManager::Profile* ProfileManager::Profile::fromJson(const std::string& name, const nlohmann::json& j) {
    Profile* cfg = new Profile();
    cfg->name = name;
    cfg->camera_selection = j.value("camera_selection", cfg->camera_selection);
    cfg->smoothing_factor = j.value("smoothing_factor", cfg->smoothing_factor);
    cfg->detection_confidence = j.value("detection_confidence", cfg->detection_confidence);
    cfg->model_selection = j.value("model_selection", cfg->model_selection);
    cfg->zoom_base = j.value("zoom_base", cfg->zoom_base);
    cfg->zoom_multiplier = j.value("zoom_multiplier", cfg->zoom_multiplier);
    cfg->target_width = j.value("target_width", cfg->target_width);
    cfg->target_height = j.value("target_height", cfg->target_height);
    cfg->virtual_camera_selection = j.value("virtual_camera_selection", cfg->virtual_camera_selection);
    return cfg;
}


std::vector<std::string> ProfileManager::getProfileList() const{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    for (const auto& profile : savedProfiles) {
        names.push_back(profile->name);
    }
    return names;
}

// Gestion des profileurations
void ProfileManager::createProfile(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto conf = findProfile(name);
    if (conf != savedProfiles.end()) {
        currentProfile = *conf;
        return;
    }

    create(name);
}

void ProfileManager::create(const std::string& name = "default")
{
    Profile* newProfile = currentProfile != nullptr ? new Profile(*currentProfile) : new Profile();
    newProfile->name = name;
    currentProfile = newProfile;
    savedProfiles.push_back(newProfile);
    internalSave();
}

void ProfileManager::deleteProfile(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = findProfile(name);
    bool isCurrent = false;
    if (it != savedProfiles.end()) {
        if (*it == currentProfile) {
            isCurrent = true;
        }
        savedProfiles.erase(it);

        if (savedProfiles.size() > 0) {
            currentProfile = savedProfiles[0];
            internalSave();
        } else {
            currentProfile = nullptr;
            create();
        }
    }
}

void ProfileManager::switchProfile(const Glib::ustring& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = findProfile(name);
    if (it != savedProfiles.end()) {
        currentProfile = *it;
        internalSave();
    }
}

// Chargement/Sauvegarde
void ProfileManager::loadProfilesFile() {
    std::lock_guard<std::mutex> lock(mutex_);
    savedProfiles.clear();
    
    if (!std::filesystem::exists(profileFilename)) {
        create();
        return;
    }

    std::ifstream inFile(profileFilename);
    nlohmann::json j;
    inFile >> j;

    // Charger toutes les profiles
    for (const auto& [key, value] : j["profiles"].items()) {
        savedProfiles.push_back(Profile::fromJson(key, value));
    }

    showQuitMessage = j.value("show_quit_message", true);

    // Appliquer la dernière profile utilisée
    std::string lastUsed = j.value("last_used", "default");
    auto it = findProfile(lastUsed);
    if (it != savedProfiles.end()) {
        currentProfile = *it;
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
    nlohmann::json j;
    j["show_quit_message"] = showQuitMessage;
    j["last_used"] = currentProfile->name;

    for (const auto& profile : savedProfiles) {
        j["profiles"][profile->name] = profile->toJson();
    }

    std::ofstream outFile(profileFilename);
    outFile << j.dump(4);
}

std::vector<ProfileManager::Profile*>::iterator ProfileManager::findProfile(const std::string& name) {
    return std::find_if(savedProfiles.begin(), savedProfiles.end(),
        [&name](const Profile* c) { return c->name == name; });
}

// Getters avec verrouillage -------------------------------
int ProfileManager::getCameraSelection() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentProfile->camera_selection;
}

double ProfileManager::getSmoothingFactor() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentProfile->smoothing_factor;
}

double ProfileManager::getDetectionConfidence() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentProfile->detection_confidence;
}

int ProfileManager::getModelSelection() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentProfile->model_selection;
}

double ProfileManager::getZoomBase() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentProfile->zoom_base;
}

double ProfileManager::getZoomMultiplier() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentProfile->zoom_multiplier;
}

int ProfileManager::getTargetWidth() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentProfile->target_width;
}

int ProfileManager::getTargetHeight() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentProfile->target_height;
}

int ProfileManager::getVirtualCameraSelection() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentProfile->virtual_camera_selection;
}

// Setters avec verrouillage -------------------------------
void ProfileManager::setCameraSelection(int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentProfile->camera_selection = value;
}

void ProfileManager::setSmoothingFactor(double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentProfile->smoothing_factor = value;
}

void ProfileManager::setDetectionConfidence(double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentProfile->detection_confidence = value;
}

void ProfileManager::setModelSelection(int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentProfile->model_selection = value;
}

void ProfileManager::setZoomBase(double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentProfile->zoom_base = value;
}

void ProfileManager::setZoomMultiplier(double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentProfile->zoom_multiplier = value;
}

void ProfileManager::setTargetWidth(int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentProfile->target_width = value;
}

void ProfileManager::setTargetHeight(int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentProfile->target_height = value;
}

void ProfileManager::setVirtualCameraSelection(int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentProfile->virtual_camera_selection = value;
}