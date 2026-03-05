#include "settings.h"
#include "patch_store.h"
#include <fstream>
#include <iostream>
#include <cstdint>

void SettingsManager::load(const std::string& gameDir, const std::string& userSettingsDir) {
    m_gameDir = gameDir;
    m_userSettingsDir = userSettingsDir;

    // Ensure config directories exist
    std::filesystem::create_directories(userSettingsDir);
    std::filesystem::create_directories(gameDir);

    std::string gamePath   = gameDir + "/game-settings.json";
    std::string globalPath = userSettingsDir + "/global-settings.json";

    // Load or create game-settings.json
    if (std::filesystem::exists(gamePath)) {
        std::ifstream file(gamePath);
        file >> m_gameSettings;
    } else {
        m_gameSettings = {
            {"game_id", "ez2dj"},
        };
    }

    // Load or create global-settings.json
    if (std::filesystem::exists(globalPath)) {
        std::ifstream file(globalPath);
        file >> m_globalSettings;
    } else {
        m_globalSettings = {
            {"button_bindings",  json::object()},
            {"analog_bindings",  json::object()},
            {"patch_delay_ms",   2000}
        };
    }

    // Load patch definitions from patches.json + user-patches.json (gameDir)
    m_patchStore.load(gameDir);
    m_patchStore.loadState(m_gameSettings.value("patches", json::object()));
}

void SettingsManager::save() const {
    // Write game-settings.json
    std::string gamePath = m_gameDir + "/game-settings.json";
    std::ofstream gameFile(gamePath);
    gameFile << m_gameSettings.dump(2);

    // Write global-settings.json
    std::string globalPath = m_userSettingsDir + "/global-settings.json";
    std::ofstream globalFile(globalPath);
    globalFile << m_globalSettings.dump(2);
}

json& SettingsManager::globalSettings() {
    return m_globalSettings;
}

json& SettingsManager::gameSettings() {
    return m_gameSettings;
}

PatchStore& SettingsManager::patchStore() {
    return m_patchStore;
}
