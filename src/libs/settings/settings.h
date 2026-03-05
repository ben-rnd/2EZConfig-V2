#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <filesystem>
#include "patch_store.h"

using json = nlohmann::json;

static constexpr int GAME_SETTINGS_VERSION  = 1;

class SettingsManager {
public:
    // Load game-settings.json, global-settings.json, patches.json, and user-patches.json.
    void load(const std::string& gameDir, const std::string& userSettingsDir);
    // Save game-settings.json and global-settings.json.
    void save() const;
    // Access to the two main JSON sections
    json& globalSettings();
    json& gameSettings();
    // Access to the typed patch store (loaded from patches.json + user-patches.json)
    PatchStore& patchStore();
private:
    std::string m_gameDir;
    std::string m_userSettingsDir;
    json m_globalSettings;
    json m_gameSettings;
    PatchStore m_patchStore;
};
