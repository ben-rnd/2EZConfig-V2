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

#include "ddraw_hook.h"
#include "dll_input.h"
#include "dll_output.h"
#include "bindings.h"
#include "input_manager.h"
#include "patch_store.h"
#include "settings.h"
#include "strings.h"
#include <nlohmann/json.hpp>

static HMODULE       s_dllModule  = nullptr;
static InputManager* s_mgr        = nullptr;
static BindingStore  s_bindings;

// All Important IO handler — intercepts IN/OUT instructions.
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
                case 0x101: ctx->Eax = (ctx->Eax & 0xFFFFFF00) | s_djPortCache[1]; break;
                case 0x102: ctx->Eax = (ctx->Eax & 0xFFFFFF00) | s_djPortCache[2]; break;
                case 0x106: ctx->Eax = (ctx->Eax & 0xFFFFFF00) | s_djPortCache[6]; break;

                //analogs
                case 0x103: ctx->Eax = (ctx->Eax & 0xFFFFFF00) | s_djPortCache[3]; break;  // P1 turntable
                case 0x104: ctx->Eax = (ctx->Eax & 0xFFFFFF00) | s_djPortCache[4]; break;  // P2 turntable

                //fall through
                default:    ctx->Eax = (ctx->Eax & 0xFFFFFF00) | 0xFF; break;
            }
            ctx->Eip += instrLen;
            return EXCEPTION_CONTINUE_EXECUTION;

        case 0xED: // IN AX, DX — Dancer read (16-bit)
            switch (port) {
                case 0x300: ctx->Eax = (ctx->Eax & 0xFFFF0000) | s_dancerPortCache[0]; break;
                case 0x302: ctx->Eax = (ctx->Eax & 0xFFFF0000) | s_dancerPortCache[1]; break;
                case 0x304: ctx->Eax = (ctx->Eax & 0xFFFF0000) | s_dancerPortCache[2]; break;
                case 0x306: ctx->Eax = (ctx->Eax & 0xFFFF0000) | s_dancerPortCache[3]; break;

                default:    ctx->Eax = (ctx->Eax & 0xFFFF0000) | 0xFFFF; break;
            }
            ctx->Eip += instrLen;
            return EXCEPTION_CONTINUE_EXECUTION;

        case 0xEE: // OUT DX, AL — DJ write (8-bit)
            handleDJOut(port, static_cast<uint8_t>(ctx->Eax & 0xFF), s_bindings);
            ctx->Eip += instrLen;
            return EXCEPTION_CONTINUE_EXECUTION;

        case 0xEF: // OUT DX, AX — Dancer write (16-bit)
            handleDancerOut(port, static_cast<uint8_t>(ctx->Eax & 0xFF), s_bindings);
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
}

static void resumeThreads(std::vector<HANDLE>& handles) {
    for (HANDLE h : handles) { ResumeThread(h); CloseHandle(h); }
    handles.clear();
}

static std::string getAppDataDir() {
    char buf[MAX_PATH] = {};
    if (GetEnvironmentVariableA("APPDATA", buf, MAX_PATH))
        return std::string(buf) + "\\2ezconfig";
    return "";
}

static DWORD WINAPI InitThread(void*) {
    // Fix innacurate sleep()
    timeBeginPeriod(1);

    // Resolve DLL directory from the DLL's own module handle.
    char dir[MAX_PATH] = {};
    GetModuleFileNameA(s_dllModule, dir, MAX_PATH);
    char* lastSlash = strrchr(dir, '\\');
    if (lastSlash) *lastSlash = '\0';

    // Resolve shared config dir: %APPDATA%\2ezconfig
    std::string appDataDir = getAppDataDir();
    if (appDataDir.empty()) return 0;

    // Exit early if shared config dir doesn't exist — 2EZConfig has never been run.
    if (GetFileAttributesA(appDataDir.c_str()) == INVALID_FILE_ATTRIBUTES)
        return 0;

    SettingsManager settings;
    try {
        settings.load(dir, appDataDir);
    } catch (...) {
        return 0;
    }

    std::string gameId = settings.gameSettings().value("game_id", "");

    // Suspend all game threads while installing time-critical hooks.
    std::vector<HANDLE> suspended;
    suspendOtherThreads(suspended);

    if (settings.globalSettings().value("force_60hz", false))
        installDDrawHook(true);

    if (settings.globalSettings().value("io_emu", true))
        AddVectoredExceptionHandler(1, IOHandler);

    if (settings.globalSettings().value("high_priority", false))
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    resumeThreads(suspended);

    // Allow dongle/hardware to stabilize before continuing.
    Sleep(settings.globalSettings().value("shim_delay", 10));

    if (!gameId.empty())
        settings.patchStore().applyEarlyPatches(gameId);

    //Setup Input manager and load bindings
    s_mgr = new InputManager();
    try {
        s_bindings.load(settings, *s_mgr);
    } catch (...) {}

    startInputPollingThread(s_bindings);
    startLightFlushThread(s_bindings, *s_mgr);

    // Wait for game init, then apply patches and version string.
    Sleep(settings.globalSettings().value("patch_delay_ms", 2000));
    settings.patchStore().applyVersionPatch("2EZConfig V2.0");
    if (!gameId.empty())
        settings.patchStore().applyPatches(gameId);

    return 0;
}

static void tryInitHardlock(HMODULE hModule) {
    char dllPath[MAX_PATH] = {};
    GetModuleFileNameA(hModule, dllPath, MAX_PATH);
    char* lastSlash = strrchr(dllPath, '\\');
    if (!lastSlash) return;
    *lastSlash = '\0';

    char settingsPath[MAX_PATH];
    snprintf(settingsPath, MAX_PATH, "%s\\game-settings.json", dllPath);

    std::ifstream f(settingsPath);
    if (!f.is_open()) return;
    nlohmann::json j;
    try { j = nlohmann::json::parse(f); } catch (...) { return; }

    if (!j.value("hardlock_enabled", false)) return;

    auto hl = j.value("hardlock", nlohmann::json::object());
    unsigned short modAd = (unsigned short)std::stoul(hl.value("ModAd", "0"), nullptr, 16);
    unsigned short seed1 = (unsigned short)std::stoul(hl.value("Seed1", "0"), nullptr, 16);
    unsigned short seed2 = (unsigned short)std::stoul(hl.value("Seed2", "0"), nullptr, 16);
    unsigned short seed3 = (unsigned short)std::stoul(hl.value("Seed3", "0"), nullptr, 16);

    if (LoadHardLockInfo(modAd, seed1, seed2, seed3) && InitHooks()) {
        //DBG_printfA("[io.hardlock]: Started!");
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        s_dllModule = hModule;
        tryInitHardlock(hModule);
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)InitThread, nullptr, 0, nullptr);
    }
    (void)reason;
    return TRUE;
}
