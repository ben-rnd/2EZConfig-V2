#include "settings.h"
#include <fstream>
#include <iostream>
#include <cstdint>

void SettingsManager::load(const std::string& gameDir, const std::string& userSettingsDir) {
    m_gameDir = gameDir;
    m_userSettingsDir = userSettingsDir;

    // Ensure config directory exists
    std::filesystem::create_directories(userSettingsDir);
    std::filesystem::create_directories(gameDir);
    
    std::string gamePath = gameDir + "/game-settings.json";
    std::string globalPath = userSettingsDir + "/global-settings.json";
    std::string patchesPath = userSettingsDir + "/patches.json";

    // Load or create settings.json
    if (std::filesystem::exists(gamePath)) {
        std::ifstream file(gamePath);
        file >> m_gameSettings;
    } else {
        // Create default structure
        m_gameSettings = {
            {"game_id", ""},
        };
    }

    if (std::filesystem::exists(globalPath)) {
        std::ifstream file(globalPath);
        file >> m_globalSettings;
    } else {
        // Create default structure
        m_globalSettings = {
            {"bindings", json::array({
                {"BUTTON_ID", "USB_ID"}
            })},
            {"usb_devices", json::array()},
            {"controller_bindings", json::object()}
        };
    }


    // Load or create patches.json
    if (std::filesystem::exists(patchesPath)) {
        std::ifstream file(patchesPath);
        file >> m_patches;
    } else {
        generateDefaultPatches();
    }
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

    // Write patches.json
    std::string patchesPath = m_userSettingsDir + "/patches.json";
    std::ofstream patchesFile(patchesPath);
    patchesFile << m_patches.dump(2);
}

json& SettingsManager::globalSettings() {
    return m_globalSettings;
}

json& SettingsManager::gameSettings() {
    return m_gameSettings;
}

json& SettingsManager::patches() {
    return m_patches;
}

void SettingsManager::generateDefaultPatches() {
    // Default patch structure with one example entry
    m_patches = {
        {"patches", json::array({
            {
                {"id", "example_patch"},
                {"name", "Example Patch"},
                {"address", "0x00000000"},
                {"original_bytes", ""},
                {"patched_bytes", ""}
            }
        })}
    };

    // Write to disk
    std::string patchesPath = m_userSettingsDir + "/patches.json";
    std::ofstream patchesFile(patchesPath);
    patchesFile << m_patches.dump(2);
}
