#include <windows.h>
#include <cstring>
#include <string>
#include <nlohmann/json.hpp>

extern "C" {
#include "io.hardlock.hooks.h"
#include "io.hardlock.emulator.h"
}

#include "shared_io.h"
#include "settings.h"
#include "logger.h"
#include "utilities.h"

static void initHardlock(SettingsManager* settings) {
    if (!settings->gameSettings().value("hardlock_enabled", false)) {
        return;
    }

    auto hardlockConfig = settings->gameSettings().value("hardlock", nlohmann::json::object());
    auto modAd = static_cast<unsigned short>(std::stoul(hardlockConfig.value("ModAd", "0"), nullptr, 16));
    auto seed1 = static_cast<unsigned short>(std::stoul(hardlockConfig.value("Seed1", "0"), nullptr, 16));
    auto seed2 = static_cast<unsigned short>(std::stoul(hardlockConfig.value("Seed2", "0"), nullptr, 16));
    auto seed3 = static_cast<unsigned short>(std::stoul(hardlockConfig.value("Seed3", "0"), nullptr, 16));

    Logger::info("[Hardlock] ModAd=0x" + toHexString(modAd) + " Seeds=0x" + toHexString(seed1) + ",0x" + toHexString(seed2) + ",0x" + toHexString(seed3));

    if (LoadHardLockInfo(modAd, seed1, seed2, seed3) && InitHooks()) {
        Logger::info("[+] Hardlock initialised");
    } else {
        Logger::error("[-] Hardlock initialisation failed");
    }
}

static void resolveRemember1st(std::string& gameId) {
    if (gameId != "ez2dj_6th") return;
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    const char* exeName = strrchr(exePath, '\\');
    exeName = exeName ? exeName + 1 : exePath;
    if (_stricmp(exeName, "EZ2DJ.exe") == 0) {
        gameId = "rmbr_1st";
        Logger::info("[Init] Remember 1st detected, using rmbr_1st patches");
    }
}

void SharedIO::earlyInit(SettingsManager* settings, std::string& gameId) {
    resolveRemember1st(gameId);
    initHardlock(settings);
}
