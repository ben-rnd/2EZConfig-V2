#include <windows.h>
#include <mmsystem.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstring>
#include <fstream>
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

static HMODULE          s_dllModule  = nullptr;
static InputManager*    s_mgr       = nullptr;
static BindingStore     s_bindings;
static SettingsManager* s_settings   = nullptr;
static std::string      s_currDirectory;
static std::string      s_gameId;

static LONG WINAPI IOHandler(PEXCEPTION_POINTERS ex) {
    if (ex->ExceptionRecord->ExceptionCode != EXCEPTION_PRIV_INSTRUCTION)
        return EXCEPTION_CONTINUE_SEARCH;

    auto* ctx      = ex->ContextRecord;
    uint8_t* eip   = reinterpret_cast<uint8_t*>(ctx->Eip);
    uint16_t port  = static_cast<uint16_t>(ctx->Edx & 0xFFFF);
    uint8_t opcode = eip[0];
    int instrLen   = 1;

    // 0x66 prefix = 16-bit operand (Dancer)
    if (opcode == 0x66) {
        opcode = eip[1];
        instrLen = 2;
    }

    switch (opcode) {

        case 0xEC: // IN AL, DX — DJ read (8-bit)
            switch (port) {
                //buttons
                case 0x101: ctx->Eax = (ctx->Eax & 0xFFFFFF00) | s_djPortCache[1].load(); break;
                case 0x102: ctx->Eax = (ctx->Eax & 0xFFFFFF00) | s_djPortCache[2].load(); break;
                case 0x106: ctx->Eax = (ctx->Eax & 0xFFFFFF00) | s_djPortCache[6].load(); break;
                //analogs
                case 0x103: ctx->Eax = (ctx->Eax & 0xFFFFFF00) | s_djPortCache[3].load(); break;  // P1 turntable
                case 0x104: ctx->Eax = (ctx->Eax & 0xFFFFFF00) | s_djPortCache[4].load(); break;  // P2 turntable
                default:
                    Logger::warnOnce("[IO] Unexpected DJ port read: 0x" + toHexString(port));
                    ctx->Eax = (ctx->Eax & 0xFFFFFF00) | 0xFF;
                    break;
            }
            ctx->Eip += instrLen;
            return EXCEPTION_CONTINUE_EXECUTION;

        case 0xED: // IN AX, DX — Dancer read (16-bit)
            switch (port) {
                case 0x300: ctx->Eax = (ctx->Eax & 0xFFFF0000) | s_dancerPortCache[0].load(); break;
                case 0x302: ctx->Eax = (ctx->Eax & 0xFFFF0000) | s_dancerPortCache[1].load(); break;
                case 0x304: ctx->Eax = (ctx->Eax & 0xFFFF0000) | s_dancerPortCache[2].load(); break;
                case 0x306: ctx->Eax = (ctx->Eax & 0xFFFF0000) | s_dancerPortCache[3].load(); break;

                default:
                    Logger::warnOnce("[IO] Unexpected Dancer port read: 0x" + toHexString(port));
                    ctx->Eax = (ctx->Eax & 0xFFFF0000) | 0xFFFF;
                    break;
            }
            ctx->Eip += instrLen;
            return EXCEPTION_CONTINUE_EXECUTION;

        case 0xEE: // OUT DX, AL — DJ write (8-bit)
            handleDJOut(port, static_cast<uint8_t>(ctx->Eax & 0xFF));
            ctx->Eip += instrLen;
            return EXCEPTION_CONTINUE_EXECUTION;

        case 0xEF: // OUT DX, AX — Dancer write (16-bit)
            handleDancerOut(port, static_cast<uint8_t>(ctx->Eax & 0xFF));
            ctx->Eip += instrLen;
            return EXCEPTION_CONTINUE_EXECUTION;

        default:
            return EXCEPTION_CONTINUE_SEARCH;
    }
}

static void suspendOtherThreads(std::vector<HANDLE>& out) {
    DWORD myTid = GetCurrentThreadId();
    DWORD pid   = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te = { sizeof(te) };
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid && te.th32ThreadID != myTid) {
                HANDLE h = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                if (h) { SuspendThread(h); out.push_back(h); }
            }
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    Logger::info("[+] Application suspended");
}

static void resumeThreads(std::vector<HANDLE>& handles) {
    for (HANDLE h : handles) { ResumeThread(h); CloseHandle(h); }
    handles.clear();
    Logger::info("[+] Application resumed");
}

static std::string getAppDataDir() {
    char buf[MAX_PATH] = {};
    if (GetEnvironmentVariableA("APPDATA", buf, MAX_PATH))
        return std::string(buf) + "\\2ezconfig";
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

    initPortCache(s_bindings);
    startLightFlushThread(s_bindings);

    Sleep(s_settings->globalSettings().value("patch_delay_ms", 2000));
    s_settings->patchStore().applyVersionPatch("2EZConfig V2.0");
    if (!s_gameId.empty()){
        s_settings->patchStore().applyPatches(s_gameId);
    }

    return 0;
}

static void applySuperEarlyPatches() {
    if (!s_settings || s_gameId.empty()) return;
    s_settings->patchStore().applySuperEarlyPatches(s_gameId);
}

static void initHardlock() {
    if (!s_settings) return;
    if (!s_settings->gameSettings().value("hardlock_enabled", false)) return;

    auto hl = s_settings->gameSettings().value("hardlock", nlohmann::json::object());
    unsigned short modAd = (unsigned short)std::stoul(hl.value("ModAd", "0"), nullptr, 16);
    unsigned short seed1 = (unsigned short)std::stoul(hl.value("Seed1", "0"), nullptr, 16);
    unsigned short seed2 = (unsigned short)std::stoul(hl.value("Seed2", "0"), nullptr, 16);
    unsigned short seed3 = (unsigned short)std::stoul(hl.value("Seed3", "0"), nullptr, 16);

    Logger::info("[Hardlock] ModAd=0x" + toHexString(modAd) + " Seeds=0x" + toHexString(seed1) + ",0x" + toHexString(seed2) + ",0x" + toHexString(seed3));

    if (LoadHardLockInfo(modAd, seed1, seed2, seed3) && InitHooks()) {
        Logger::info("[+] Hardlock initialised");
    } else {
        Logger::error("[-] Hardlock initialisation failed");
    }
}

static void initLogger() {
    if (!s_settings) return;
    bool loggingEnabled = s_settings->gameSettings().value("logging_enabled", false);
    Logger::init(s_currDirectory, loggingEnabled, "2ez-logs.txt");
}

static void loadSettings(HMODULE hModule) {
    char dllPath[MAX_PATH] = {};
    GetModuleFileNameA(hModule, dllPath, MAX_PATH);
    char* lastSlash = strrchr(dllPath, '\\');
    if (!lastSlash) return;
    *lastSlash = '\0';
    s_currDirectory = dllPath;

    std::string appDataDir = getAppDataDir();
    if (appDataDir.empty()) return;

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

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        s_dllModule = hModule;
        loadSettings(hModule);
        initLogger();
        initHardlock();
        applySuperEarlyPatches();
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)InitThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
