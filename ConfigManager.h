#pragma once

#include <mutex>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <algorithm>

class ConfigManager {
public:
    explicit ConfigManager(const std::string& configFilePath);
    
    // Gestion des configurations
    void createConfig(const std::string& name);
    void deleteConfig(const std::string& name);
    void switchConfig(const std::string& name);
    std::vector<std::string> getConfigList() const;

    // Getters/Setters pour la configuration courante
    // Getters
    int getCameraSelection() const;
    double getSmoothingFactor() const;
    double getDetectionConfidence() const;
    int getModelSelection() const;
    double getZoomBase() const;
    double getZoomMultiplier() const;
    int getTargetWidth() const;
    int getTargetHeight() const;
    int getVirtualCameraSelection() const;
    
    // Setters
    void setCameraSelection(int value);
    void setSmoothingFactor(double value);
    void setDetectionConfidence(double value);
    void setModelSelection(int value);
    void setZoomBase(double value);
    void setZoomMultiplier(double value);
    void setTargetWidth(int value);
    void setTargetHeight(int value);
    void setVirtualCameraSelection(int value);

    void loadConfig();
    void saveConfig();

private:
    struct Config {
        // Paramètres
        int camera_selection = 0;
        double smoothing_factor = 0.05;
        double detection_confidence = 0.3;
        int model_selection = 1;
        double zoom_base = 1.5;
        double zoom_multiplier = 1.0;
        int target_width = 1280;
        int target_height = 720;
        int virtual_camera_selection = 20;
        std::string name = "default";

        // Conversion JSON
        nlohmann::json toJson() const;
        static Config fromJson(const nlohmann::json& j);
    };

    mutable std::mutex mutex_;
    std::string configFilename;
    Config currentConfig;
    std::vector<Config> savedConfigs;

    // Helpers
    void applyConfig(const Config& config);
    void internalSave();
    Config getDefaultConfig() const { return Config(); }
    std::vector<Config>::iterator findConfig(const std::string& name);
};