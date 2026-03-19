#include <windows.h>
#include <mmsystem.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

extern "C" {
#include "io.hardlock.hooks.h"
#include "io.hardlock.emulator.h"
}

#include "dll_input.h"
#include "dll_output.h"
#include "bindings.h"
#include "input_manager.h"
#include "patch_store.h"
#include "settings.h"
#include "game_defs.h"
#include "logger.h"
#include "utilities.h"

static HMODULE s_dllModule = nullptr;
static InputManager* s_mgr = nullptr;
static BindingStore s_bindings;
static SettingsManager* s_settings = nullptr;
static std::string s_currDirectory;
static std::string s_gameId;

static LONG WINAPI IOHandler(PEXCEPTION_POINTERS ex) {
    if (ex->ExceptionRecord->ExceptionCode != EXCEPTION_PRIV_INSTRUCTION) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    auto* context = ex->ContextRecord;
    uint8_t* instructionPtr = reinterpret_cast<uint8_t*>(context->Eip);
    uint16_t port = static_cast<uint16_t>(context->Edx & 0xFFFF);
    uint8_t opcode = instructionPtr[0];
    int instructionLength = 1;

    // 0x66 prefix = 16-bit operand (Dancer)
    if (opcode == 0x66) {
        opcode = instructionPtr[1];
        instructionLength = 2;
    }

    switch (opcode) {

        case 0xEC: { // IN AL, DX — DJ input (8-bit)
            uint8_t value;
            handleDJIn(port, value);
            context->Eax = (context->Eax & 0xFFFFFF00) | value;
            context->Eip += instructionLength;
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        case 0xED: { // IN AX, DX — Dancer input (16-bit)
            uint16_t value;
            handleDancerIn(port, value);
            context->Eax = (context->Eax & 0xFFFF0000) | value;
            context->Eip += instructionLength;
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        case 0xEE: // OUT DX, AL — DJ lights (8-bit)
            handleDJOut(port, static_cast<uint8_t>(context->Eax & 0xFF));
            context->Eip += instructionLength;
            return EXCEPTION_CONTINUE_EXECUTION;

        case 0xEF: // OUT DX, AX — Dancer lights (16-bit)
            handleDancerOut(port, static_cast<uint16_t>(context->Eax & 0xFFFF));
            context->Eip += instructionLength;
            return EXCEPTION_CONTINUE_EXECUTION;

        default:
            return EXCEPTION_CONTINUE_SEARCH;
    }
}

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

    Logger::info("[Init] io_emu=" + std::to_string(s_settings->globalSettings().value("io_emu", true))
        + " high_priority=" + std::to_string(s_settings->globalSettings().value("high_priority", false))
        + " patch_delay_ms=" + std::to_string(s_settings->globalSettings().value("patch_delay_ms", 2000))
        + " shim_delay=" + std::to_string(s_settings->globalSettings().value("shim_delay", 10)));

    std::vector<HANDLE> suspended;
    suspendOtherThreads(suspended);

    timeBeginPeriod(1);

    if (s_settings->globalSettings().value("io_emu", true)){
        AddVectoredExceptionHandler(1, IOHandler);
        Logger::info("[+] IO Hook initilaised");
    }

    if (s_settings->globalSettings().value("high_priority", false)){
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        Logger::info("[+] High Priority process initilaised");
    }

    resumeThreads(suspended);

    // Allow dongle/hardware to stabilize before continuing.
    Sleep(s_settings->globalSettings().value("shim_delay", 10));

    if (!s_gameId.empty()){
        s_settings->patchStore().applyEarlyPatches(s_gameId);
    }

    s_mgr = new InputManager();
    try {
        s_bindings.load(*s_settings, *s_mgr);
        Logger::info("[+] Bindings loaded successfully");
    } catch (...) {
        Logger::error("[-] Bindings failed to load");
    }

    startInputPollThread(s_bindings);
    startLightFlushThread(s_bindings);

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

static void initHardlock() {
    if (!s_settings) {
        return;
    }
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

static void initLogger() {
    if (!s_settings) {
        return;
    }
    bool loggingEnabled = s_settings->gameSettings().value("logging_enabled", false);
    Logger::init(s_currDirectory, loggingEnabled, "2ez-logs.txt");
    bool verboseOutput = s_settings->gameSettings().value("verbose_output_logging", false);
    initOutputLogging(verboseOutput);
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

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        s_dllModule = hModule;
        loadSettings(hModule);
        resolveRemember1st();
        initLogger();
        initHardlock();
        applySuperEarlyPatches();
        CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(InitThread), nullptr, 0, nullptr);
    }
    return TRUE;
}
