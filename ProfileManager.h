#pragma once

#include "glibmm/ustring.h"
#include <mutex>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <algorithm>

class ProfileManager {
public:
    explicit ProfileManager(const std::string& profileFilePath);
    
    // Gestion des profils
    void createProfile(const std::string& name = "default");
    void deleteProfile(const std::string& name);
    void switchProfile(const Glib::ustring& name);
    std::vector<std::string> getProfileList() const;
    std::string getCurrentProfileName() const { return currentProfile->name; }
    bool profileExists(const std::string& name) { return findProfile(name) != savedProfiles.end(); }

    // Getters/Setters pour la profil courante
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

    void loadProfilesFile();
    void saveProfileFiles();

private:
    struct Profile {
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
        static Profile* fromJson(const std::string& name, const nlohmann::json& j);
    };

    mutable std::mutex mutex_;
    std::string profileFilename;
    Profile* currentProfile = nullptr;
    std::vector<Profile*> savedProfiles;

    // Helpers
    void applyProfile(const Profile& profile);
    void internalSave();
    void create(const std::string& name);

    std::vector<Profile*>::iterator findProfile(const std::string& name);
};