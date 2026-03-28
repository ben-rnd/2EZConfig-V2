#include <windows.h>
#include <mmsystem.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "shared_io.h"
#include "ez2dj_io.h"
#include "ez2dancer_io.h"
#include "sabin_io.h"
#include "bindings.h"
#include "input_manager.h"
#include "patch_store.h"
#include "settings.h"
#include "game_defs.h"
#include "logger.h"

extern "C" __declspec(dllexport) void hook_init(void) {}

static HMODULE s_dllModule = nullptr;
static InputManager* s_input = nullptr;
static BindingStore s_bindings;
static SettingsManager* s_settings = nullptr;
static std::string s_currDirectory;
static std::string s_gameId;

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
    if (!s_settings) {
        Logger::error("[-] Settings not loaded, InitThread aborting");
        return 0;
    }

    std::vector<HANDLE> suspended;
    suspendOtherThreads(suspended);

    timeBeginPeriod(1);

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

    // Dispatch to the appropriate IO handler based on game family
    GameFamily family = familyFromGameId(s_gameId);
    switch (family) {
        case GameFamily::EZ2DJ:
            EZ2DJIO::installHooks(&s_bindings, s_input, s_settings);
            break;
        case GameFamily::EZ2Dancer:
            EZ2DancerIO::installHooks(&s_bindings, s_input, s_settings);
            break;
        case GameFamily::SabinSS:
            SabinIO::installHooks(&s_bindings, s_input);
            break;
    }

    resumeThreads(suspended);

    // Allow dongle/hardware to stabilize before continuing.
    Sleep(s_settings->globalSettings().value("shim_delay", 10));

    if (!s_gameId.empty()){
        s_settings->patchStore().applyEarlyPatches(s_gameId);
    }

    Sleep(s_settings->globalSettings().value("patch_delay_ms", 2000));
    s_settings->patchStore().applyVersionPatch("2EZConfig V2.0");
    if (!s_gameId.empty()){
        s_settings->patchStore().applyPatches(s_gameId);
    }

    return 0;
}

static void applySuperEarlyPatches() {
    if (!s_settings || s_gameId.empty()) {
        return;
    }
    s_settings->patchStore().applySuperEarlyPatches(s_gameId);
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
        s_dllModule = hModule;
        loadSettings(hModule);

        // EZ2-specific early init (hardlock + remember1st) — must run before threads
        GameFamily family = familyFromGameId(s_gameId);
        if (s_settings && (family == GameFamily::EZ2DJ || family == GameFamily::EZ2Dancer)) {
            SharedIO::earlyInit(s_settings, s_gameId);
        }

        initLogger();
        applySuperEarlyPatches();
        CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(InitThread), nullptr, 0, nullptr);
    }
    return TRUE;
}
