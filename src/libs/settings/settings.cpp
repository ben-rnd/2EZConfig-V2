#include "settings.h"
#include "patch_store.h"
#include "logger.h"
#include <filesystem>
#include <fstream>

void SettingsManager::load(const std::string& gameDir, const std::string& userSettingsDir) {
    m_gameDir = gameDir;
    m_userSettingsDir = userSettingsDir;

    std::filesystem::create_directories(userSettingsDir);
    std::filesystem::create_directories(gameDir);

    std::string gamePath   = gameDir + "/game-settings.json";
    std::string globalPath = userSettingsDir + "/global-settings.json";

    bool gameNew = !std::filesystem::exists(gamePath);
    if (!gameNew) {
        std::ifstream file(gamePath);
        file >> m_gameSettings;
    } else {
        m_gameSettings = {
            {"logging_enabled", false},
            {"skip_ui", false}
        };
    }

    Logger::info("[Settings] Game settings: " + gamePath + (gameNew ? " (new)" : ""));

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

    Logger::info("[Settings] Global settings: " + globalPath + (globalNew ? " (new)" : ""));

    // Load patch definitions from patches.json (appdata) + user-patches.json (game dir)
    m_patchStore.load(userSettingsDir, m_gameDir);
    m_patchStore.loadState(m_gameSettings.value("patches", json::object()));

    // Write defaults to disk immediately if either file was missing
    if (gameNew || globalNew) {
        save();
    }
}

void SettingsManager::save() const {
    std::string gamePath = m_gameDir + "/game-settings.json";
    std::ofstream gameFile(gamePath);
    gameFile << m_gameSettings.dump(2);

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
