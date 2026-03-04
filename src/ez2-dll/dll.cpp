#include <windows.h>
#include <mmsystem.h>
#include <cstdint>
#include <cstring>
#include <string>

#include "dll_input.h"
#include "dll_output.h"
#include "bindings.h"
#include "input_manager.h"
#include "settings.h"

static HMODULE       s_dllModule   = nullptr;
static InputManager* s_mgr         = nullptr;
static BindingStore  s_bindings;
static volatile bool s_initialized = false;

// Combined VEH handler dispatching DJ (0xEC/0xEE) and Dancer (0x66+0xED/0x66+0xEF) opcodes.
// IN reads come from pre-computed volatile cache — zero function calls on game thread.
// OUT writes go to shadow buffers — flushed by background threads.
static LONG WINAPI CombinedIOHandler(PEXCEPTION_POINTERS ex) {
    if (ex->ExceptionRecord->ExceptionCode != EXCEPTION_PRIV_INSTRUCTION)
        return EXCEPTION_CONTINUE_SEARCH;

    if (!s_initialized)
        return EXCEPTION_CONTINUE_SEARCH;

    uint8_t* eip   = reinterpret_cast<uint8_t*>(ex->ContextRecord->Eip);
    uint16_t port  = static_cast<uint16_t>(ex->ContextRecord->Edx & 0xFFFF);

    if (eip[0] == 0x66) {
        if (eip[1] == 0xED) {
            // IN AX, DX — Dancer IN: read from pre-computed cache
            uint16_t result;
            if (port >= 0x300 && port <= 0x306 && ((port & 1) == 0)) {
                result = s_dancerPortCache[(port - 0x300) >> 1];
            } else {
                result = 0xFFFF;
            }
            ex->ContextRecord->Eax = (ex->ContextRecord->Eax & 0xFFFF0000) | result;
            ex->ContextRecord->Eip += 2;
            return EXCEPTION_CONTINUE_EXECUTION;
        } else if (eip[1] == 0xEF) {
            // OUT DX, AX — Dancer OUT: write to shadow buffer
            uint8_t value = static_cast<uint8_t>(ex->ContextRecord->Eax & 0xFF);
            handleDancerOut(port, value, s_bindings);
            ex->ContextRecord->Eip += 2;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    } else if (eip[0] == 0xEC) {
        // IN AL, DX — DJ IN: read from pre-computed cache
        uint8_t result;
        if (port >= 0x100 && port <= 0x106) {
            result = s_djPortCache[port & 0x07];
        } else {
            result = 0xFF;
        }
        ex->ContextRecord->Eax = (ex->ContextRecord->Eax & 0xFFFFFF00) | result;
        ex->ContextRecord->Eip += 1;
        return EXCEPTION_CONTINUE_EXECUTION;
    } else if (eip[0] == 0xEE) {
        // OUT DX, AL — DJ OUT: write to shadow buffer
        uint8_t value = static_cast<uint8_t>(ex->ContextRecord->Eax & 0xFF);
        handleDJOut(port, value, s_bindings);
        ex->ContextRecord->Eip += 1;
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

static DWORD WINAPI InitThread(void*) {
    // Set system timer resolution to 1ms so Sleep(1) actually sleeps ~1ms
    // instead of the default ~15.6ms. Critical for polling thread accuracy.
    timeBeginPeriod(1);

    // Brief yield for game to stabilize.
    Sleep(10);

    // Resolve DLL directory from the DLL's own module handle.
    char dir[MAX_PATH] = {};
    GetModuleFileNameA(s_dllModule, dir, MAX_PATH);
    // Strip filename — keep directory path.
    char* lastSlash = strrchr(dir, '\\');
    if (lastSlash) *lastSlash = '\0';

    // Check if global-settings.json exists in DLL directory.
    std::string settingsPath = std::string(dir) + "\\global-settings.json";
    bool settingsFound = (GetFileAttributesA(settingsPath.c_str()) != INVALID_FILE_ATTRIBUTES);

    // Load settings and bindings if settings file exists.
    bool settingsLoaded = false;
    SettingsManager settings;
    if (settingsFound) {
        try {
            settings.load(dir, dir);
            settingsLoaded = true;
        } catch (...) {
            // Fall through to all-released state (no crash).
        }
    }

    // Create InputManager — constructor waits for HWND to be ready (up to 500ms).
    s_mgr = new InputManager();

    // Load bindings from settings if available.
    if (settingsLoaded) {
        try {
            s_bindings.load(settings, *s_mgr,
                            s_ioButtonNames,      IO_BUTTON_COUNT,
                            s_dancerButtonNames,  DANCER_BUTTON_COUNT,
                            s_lightNames,         LIGHT_NAME_COUNT);
        } catch (...) {
            // Fall through to all-released state (no crash).
        }
    }

    // Start background threads for input polling and light flushing.
    // VEH handler reads/writes only volatile caches — zero blocking on game thread.
    startInputPollingThread(s_bindings, *s_mgr);
    startLightFlushThread(s_bindings, *s_mgr);

    // Register VEH handler LAST — after all threads and caches are ready.
    AddVectoredExceptionHandler(1, CombinedIOHandler);
    s_initialized = true;

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        s_dllModule = hModule;
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
