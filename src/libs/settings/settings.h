#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <filesystem>

using json = nlohmann::json;

static constexpr int GAME_SETTINGS_VERSION  = 1;                                                                 
static constexpr int PATCHES_VERSION        = 1;    
class SettingsManager {
public:
    // Load settings.json and patches.json from configDir.
    // If patches.json doesn't exist, generate it from embedded defaults.
    void load(const std::string& gameDir, const std::string& userSettingsDir);
    // Save settings.json and patches.json
    void save() const;
    // Access to the three main JSON sections
    json& globalSettings(); // controller bindings, USB devices
    json& gameSettings();
    json& patches();
private:
    std::string m_gameDir;
    std::string m_userSettingsDir;
    json m_globalSettings;
    json m_gameSettings;  // keyed by game id
    json m_patches;

    void generateDefaultPatches();
};
