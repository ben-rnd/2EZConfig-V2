#include <windows.h>
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
static LONG WINAPI CombinedIOHandler(PEXCEPTION_POINTERS ex) {
    // EXCEPTION_PRIV_INSTRUCTION guard — first check, fastest exit for non-I/O exceptions.
    if (ex->ExceptionRecord->ExceptionCode != EXCEPTION_PRIV_INSTRUCTION)
        return EXCEPTION_CONTINUE_SEARCH;

    // Pre-init safety: bindings and InputManager not yet ready.
    if (!s_initialized)
        return EXCEPTION_CONTINUE_SEARCH;

    uint8_t* eip   = reinterpret_cast<uint8_t*>(ex->ContextRecord->Eip);
    uint16_t port  = static_cast<uint16_t>(ex->ContextRecord->Edx & 0xFFFF);

    if (eip[0] == 0x66) {
        // 16-bit Dancer opcodes
        if (eip[1] == 0xED) {
            // IN AX, DX (0x66 0xED) — Dancer IN
            uint16_t result = computeDancerPortWord(port, s_bindings, *s_mgr);
            ex->ContextRecord->Eax = (ex->ContextRecord->Eax & 0xFFFF0000) | result;
            ex->ContextRecord->Eip += 2;
            return EXCEPTION_CONTINUE_EXECUTION;
        } else if (eip[1] == 0xEF) {
            // OUT DX, AX (0x66 0xEF) — Dancer OUT
            uint8_t value = static_cast<uint8_t>(ex->ContextRecord->Eax & 0xFF);
            handleDancerOut(port, value, s_bindings, *s_mgr);
            ex->ContextRecord->Eip += 2;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    } else if (eip[0] == 0xEC) {
        // IN AL, DX — DJ IN (8-bit)
        uint8_t result = computeDJPortByte(port, s_bindings, *s_mgr);
        ex->ContextRecord->Eax = (ex->ContextRecord->Eax & 0xFFFFFF00) | result;
        ex->ContextRecord->Eip += 1;
        return EXCEPTION_CONTINUE_EXECUTION;
    } else if (eip[0] == 0xEE) {
        // OUT DX, AL — DJ OUT (8-bit)
        uint8_t value = static_cast<uint8_t>(ex->ContextRecord->Eax & 0xFF);
        handleDJOut(port, value, s_bindings, *s_mgr);
        ex->ContextRecord->Eip += 1;
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

static DWORD WINAPI InitThread(void*) {
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

    // Register VEH handler LAST — after InputManager and BindingStore are ready.
    // Position 1 = first in handler chain.
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
