#include <windows.h>
#include <mmsystem.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "ez2dj_io.h"
#include "ez2dancer_io.h"
#include "sabin_io.h"
#include "bindings.h"
#include "input_manager.h"
#include "patch_store.h"
#include "settings.h"
#include "game_defs.h"
#include "logger.h"

extern "C" {
#include "io.hardlock.hooks.h"
#include "io.hardlock.emulator.h"
}
#include "utilities.h"
#include "ddraw3_fix.h"
#include "ddraw7_fix.h"
#include "hooks.h"

extern "C" __declspec(dllexport) void hook_init(void) {}

static InputManager* s_input = nullptr;
static BindingStore s_bindings;
static SettingsManager* s_settings = nullptr;
static std::string s_currDirectory;
static std::string s_gameId;
static GameFamily s_gameFamily;

static void suspendOtherThreads(std::vector<HANDLE>& out) {
    DWORD myTid = GetCurrentThreadId();
    DWORD processId   = GetCurrentProcessId();
    HANDLE threadSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (threadSnapshot == INVALID_HANDLE_VALUE) {
        return;
    }
    THREADENTRY32 threadEntry = { sizeof(threadEntry) };
    if (Thread32First(threadSnapshot, &threadEntry)) {
        do {
            if (threadEntry.th32OwnerProcessID == processId && threadEntry.th32ThreadID != myTid) {
                HANDLE threadHandle = OpenThread(THREAD_SUSPEND_RESUME, FALSE, threadEntry.th32ThreadID);
                if (threadHandle) {
                    SuspendThread(threadHandle);
                    out.push_back(threadHandle);
                }
            }
        } while (Thread32Next(threadSnapshot, &threadEntry));
    }
    CloseHandle(threadSnapshot);
    Logger::info("[+] Application suspended");
}

static void resumeThreads(std::vector<HANDLE>& handles) {
    for (HANDLE threadHandle : handles) {
        ResumeThread(threadHandle);
        CloseHandle(threadHandle);
    }
    handles.clear();
    Logger::info("[+] Application resumed");
}

static std::string getAppDataDir() {
    char pathBuffer[MAX_PATH] = {};
    if (GetEnvironmentVariableA("APPDATA", pathBuffer, MAX_PATH)) {
        return std::string(pathBuffer) + "\\2ezconfig";
    }
    return "";
}

static DWORD WINAPI InitThread(void*) {
    timeBeginPeriod(1);

    if (!s_settings) {
        Logger::error("[-] Settings not loaded, InitThread aborting");
        return 0;
    }

    if (s_settings->globalSettings().value("high_priority", false)){
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        Logger::info("[+] High Priority process initialised");
    }

    s_input = new InputManager();
    try {
        s_bindings.load(*s_settings, *s_input);
        Logger::info("[+] Bindings loaded successfully");
    } catch (...) {
        Logger::error("[-] Bindings failed to load");
    }

    //Initialise the appropriate IO handler based on game family
    switch (s_gameFamily) {
        case GameFamily::EZ2DJ:
            EZ2DJIO::initialiseIO(&s_bindings, s_input, s_settings);
            break;
        case GameFamily::EZ2Dancer:
            EZ2DancerIO::initialiseIO(&s_bindings, s_input, s_settings);
            break;
        case GameFamily::SabinSS:
            SabinIO::initialiseIO(&s_bindings, s_input);
            break;
    }
    
    //Apply patches that need to be run after decryption is finished.
    // 10ms seems to work reliably.
    Sleep(s_settings->globalSettings().value("shim_delay", 10));
    s_settings->patchStore().applyEarlyPatches(s_gameId);
    


    //Apply non critical patches that rely on game state being established. Apply last.
    Sleep(s_settings->globalSettings().value("patch_delay_ms", 2000));
    if(s_gameFamily == GameFamily::EZ2DJ || s_gameFamily == GameFamily::EZ2Dancer){
        s_settings->patchStore().applyVersionPatch("2EZConfig V2.0");
    }
    
    s_settings->patchStore().applyPatches(s_gameId);

    return 0;
}

static void applySuperEarlyPatches() {
    if (!s_settings || s_gameId.empty()) {
        return;
    }
    s_settings->patchStore().applySuperEarlyPatches(s_gameId);
}

static void initHardlock() {
    if (!s_settings->gameSettings().value("hardlock_enabled", false)) {
        return;
    }
    auto hardlockConfig = s_settings->gameSettings().value("hardlock", nlohmann::json::object());
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

static void resolveRemember1st() {
    if (s_gameId != "ez2dj_6th") return;
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    const char* exeName = strrchr(exePath, '\\');
    exeName = exeName ? exeName + 1 : exePath;
    if (_stricmp(exeName, "EZ2DJ.exe") == 0) {
        s_gameId = "rmbr_1st";
        Logger::info("[Init] Remember 1st detected, using rmbr_1st patches");
    }
}

static void earlyInit() {

    if (!s_settings) {
        return;
    }

    applySuperEarlyPatches();

    GameFamily family = familyFromGameId(s_gameId);
    switch (family) {
        case GameFamily::EZ2DJ:
            resolveRemember1st();
            initHardlock();
            EZ2DJIO::installHooks(s_settings);
            // DDraw3 era (1st SE, Remember 1st) vs DDraw7 era (2nd TraX onwards).
            // Mutually exclusive: both hooks target ddraw.dll's shared QueryInterface
            // dispatcher, which would collide under MinHook if installed together.
            if (s_gameId == "ez2dj_1st_se" || s_gameId == "rmbr_1st") {
                DDraw3Fix::install(s_gameId,
                    s_settings->gameSettings().value("ddraw3_force_32bpp", false),
                    s_settings->gameSettings().value("ddraw3_force_60hz", false),
                    s_settings->gameSettings().value("ddraw3_point_filtering", false),
                    s_settings->gameSettings().value("ddraw3_texel_alignment", false));
            } else {
                DDraw7Fix::install(
                    s_settings->gameSettings().value("force_32bit_display", false),
                    s_settings->gameSettings().value("force_60hz", false),
                    s_settings->gameSettings().value("point_filtering", false),
                    s_settings->gameSettings().value("texel_alignment", false));
            }
            break;
        case GameFamily::EZ2Dancer:
            initHardlock();
            EZ2DancerIO::installHooks(s_settings);
            DDraw7Fix::install(
                s_settings->gameSettings().value("force_32bit_display", false),
                s_settings->gameSettings().value("force_60hz", false),
                s_settings->gameSettings().value("point_filtering", false),
                s_settings->gameSettings().value("texel_alignment", false));
            break;
        case GameFamily::SabinSS:
            SabinIO::installHooks();
            break;
    }
}

static void loadSettings(HMODULE hModule) {
    char dllPath[MAX_PATH] = {};
    GetModuleFileNameA(hModule, dllPath, MAX_PATH);
    char* lastSlash = strrchr(dllPath, '\\');
    if (!lastSlash) {
        return;
    }
    *lastSlash = '\0';
    s_currDirectory = dllPath;

    std::string appDataDir = getAppDataDir();
    if (appDataDir.empty()) {
        return;
    }

    Logger::info("[Init] AppData dir: " + appDataDir);

    // Exit early if shared config dir doesn't exist — 2EZConfig has never been run.
    if (GetFileAttributesA(appDataDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Logger::warn("[Init] AppData dir not found, aborting settings load");
        return;
    }

    s_settings = new SettingsManager();
    try {
        s_settings->load(s_currDirectory, appDataDir);
    } catch (...) {
        Logger::error("[Init] Failed to load settings from " + s_currDirectory);
        delete s_settings;
        s_settings = nullptr;
        return;
    }

    s_gameId = s_settings->gameSettings().value("game_id", "");
    s_gameFamily = familyFromGameId(s_gameId);

    Logger::info("[Init] DLL dir: " + s_currDirectory);
    Logger::info("[Init] Game ID: " + (s_gameId.empty() ? "(none)" : s_gameId));
}

static void initLogger() {
    if (!s_settings) {
        return;
    }
    bool loggingEnabled = s_settings->gameSettings().value("logging_enabled", false);
    Logger::init(s_currDirectory, loggingEnabled, "2ez-logs.txt");
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        loadSettings(hModule);
        initLogger();
        hooks_init();
        earlyInit();
        CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(InitThread), nullptr, 0, nullptr);
    } else if (reason == DLL_PROCESS_DETACH) {
        hooks_shutdown();
    }
    return TRUE;
}
