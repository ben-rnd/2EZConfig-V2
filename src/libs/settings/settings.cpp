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
    bool gameNew = !std::filesystem::exists(gamePath);
    if (!gameNew) {
        std::ifstream file(gamePath);
        file >> m_gameSettings;
    } else {
        m_gameSettings = {
            {"game_id", "ez2dj"},
            {"hardlock_enabled", false},
            {"hardlock", {
                {"ModAd", "0"},
                {"Seed1", "0"},
                {"Seed2", "0"},
                {"Seed3", "0"}
            }}
        };
    }

    // Load or create global-settings.json
    bool globalNew = !std::filesystem::exists(globalPath);
    if (!globalNew) {
        std::ifstream file(globalPath);
        file >> m_globalSettings;
    } else {
        m_globalSettings = {
            {"button_bindings",  json::object()},
            {"analog_bindings",  json::object()},
            {"patch_delay_ms",   2000}
        };
    }

    // Load patch definitions from patches.json + user-patches.json (shared appdata dir)
    m_patchStore.load(userSettingsDir);
    m_patchStore.loadState(m_gameSettings.value("patches", json::object()));

    // Write defaults to disk immediately if either file was missing
    if (gameNew || globalNew) save();
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
