#include "ConfigManager.h"
#include "glibmm/ustring.h"

ConfigManager::ConfigManager(const std::string& configFilePath) 
    : configFilename(configFilePath), currentConfig(getDefaultConfig()) {
    loadConfig();
}

// Conversion Config <-> JSON
nlohmann::json ConfigManager::Config::toJson() const {
    return {
        {"name", name},
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

ConfigManager::Config* ConfigManager::Config::fromJson(const nlohmann::json& j) {
    Config* cfg = new Config();
    cfg->name = j.value("name", cfg->name);
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


std::vector<std::string> ConfigManager::getConfigList() const{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    for (const auto& config : savedConfigs) {
        names.push_back(config->name);
    }
    return names;
}

// Gestion des configurations
void ConfigManager::createConfig(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto conf = findConfig(name);
    if (conf != savedConfigs.end()) {
        currentConfig = *conf;
        return;
    }
    
    Config* newConfig = new Config();
    newConfig->name = name;
    currentConfig = newConfig;
    savedConfigs.push_back(newConfig);
}

void ConfigManager::deleteConfig(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = findConfig(name);
    if (it != savedConfigs.end() && name != "default") {
        savedConfigs.erase(it);
    }
}

void ConfigManager::switchConfig(const Glib::ustring& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = findConfig(name);
    if (it != savedConfigs.end()) {
        currentConfig = *it;
    }
}

// Chargement/Sauvegarde
void ConfigManager::loadConfig() {
    std::lock_guard<std::mutex> lock(mutex_);
    savedConfigs.clear();
    
    if (!std::filesystem::exists(configFilename)) {
        createConfig();
        return;
    }

    std::ifstream inFile(configFilename);
    nlohmann::json j;
    inFile >> j;

    // Charger toutes les configs
    for (const auto& [key, value] : j["configs"].items()) {
        savedConfigs.push_back(Config::fromJson(value));
    }

    // Appliquer la dernière config utilisée
    std::string lastUsed = j.value("last_used", "default");
    auto it = findConfig(lastUsed);
    currentConfig = (it != savedConfigs.end()) ? *it : getDefaultConfig();
}

void ConfigManager::saveConfig() {
    std::lock_guard<std::mutex> lock(mutex_);
    internalSave();
}

// Helpers privés
void ConfigManager::internalSave() {
    nlohmann::json j;
    j["last_used"] = currentConfig->name;

    for (const auto& config : savedConfigs) {
        j["configs"][config->name] = config->toJson();
    }

    std::ofstream outFile(configFilename);
    outFile << j.dump(4);
}

std::vector<ConfigManager::Config*>::iterator ConfigManager::findConfig(const std::string& name) {
    return std::find_if(savedConfigs.begin(), savedConfigs.end(),
        [&name](const Config* c) { return c->name == name; });
}

// Getters avec verrouillage -------------------------------
int ConfigManager::getCameraSelection() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentConfig->camera_selection;
}

double ConfigManager::getSmoothingFactor() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentConfig->smoothing_factor;
}

double ConfigManager::getDetectionConfidence() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentConfig->detection_confidence;
}

int ConfigManager::getModelSelection() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentConfig->model_selection;
}

double ConfigManager::getZoomBase() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentConfig->zoom_base;
}

double ConfigManager::getZoomMultiplier() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentConfig->zoom_multiplier;
}

int ConfigManager::getTargetWidth() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentConfig->target_width;
}

int ConfigManager::getTargetHeight() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentConfig->target_height;
}

int ConfigManager::getVirtualCameraSelection() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentConfig->virtual_camera_selection;
}

// Setters avec verrouillage -------------------------------
void ConfigManager::setCameraSelection(int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentConfig->camera_selection = value;
}

void ConfigManager::setSmoothingFactor(double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentConfig->smoothing_factor = value;
}

void ConfigManager::setDetectionConfidence(double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentConfig->detection_confidence = value;
}

void ConfigManager::setModelSelection(int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentConfig->model_selection = value;
}

void ConfigManager::setZoomBase(double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentConfig->zoom_base = value;
}

void ConfigManager::setZoomMultiplier(double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentConfig->zoom_multiplier = value;
}

void ConfigManager::setTargetWidth(int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentConfig->target_width = value;
}

void ConfigManager::setTargetHeight(int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentConfig->target_height = value;
}

void ConfigManager::setVirtualCameraSelection(int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentConfig->virtual_camera_selection = value;
}