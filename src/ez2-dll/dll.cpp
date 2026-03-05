#include <windows.h>
#include <mmsystem.h>
#include <cstdint>
#include <cstring>
#include <string>

#include "dll_input.h"
#include "dll_output.h"
#include "bindings.h"
#include "input_manager.h"
#include "patch_store.h"
#include "settings.h"
#include "strings.h"

static HMODULE       s_dllModule          = nullptr;
static InputManager* s_mgr                = nullptr;
static BindingStore  s_bindings;
static DWORD         s_originalRefreshRate = 0;

// VEH handler — intercepts privileged IN/OUT instructions.
// Opcodes: 0xEC = IN AL,DX (DJ)  0xEE = OUT DX,AL (DJ)
//          0x66 0xED = IN AX,DX (Dancer)  0x66 0xEF = OUT DX,AX (Dancer)
static LONG WINAPI CombinedIOHandler(PEXCEPTION_POINTERS ex) {
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

    // Force 60Hz refresh rate if enabled in global settings.
    if (settingsLoaded && settings.globalSettings().value("force_60hz", false)) {
        DEVMODEA dm = {};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
            s_originalRefreshRate = dm.dmDisplayFrequency;
            if (dm.dmDisplayFrequency != 60) {
                dm.dmDisplayFrequency = 60;
                dm.dmFields = DM_DISPLAYFREQUENCY;
                ChangeDisplaySettingsA(&dm, CDS_FULLSCREEN);
            }
        }
    }

    // Set high priority if enabled in global settings.
    if (settingsLoaded && settings.globalSettings().value("high_priority", false)) {
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    }

    // Create InputManager — constructor waits for HWND to be ready (up to 500ms).
    s_mgr = new InputManager();

    // Load bindings from settings if available.
    if (settingsLoaded) {
        try {
            static const int IO_COUNT      = (int)(sizeof(ioButtons)          / sizeof(ioButtons[0]));
            static const int DANCER_COUNT  = (int)(sizeof(ez2DancerIOButtons) / sizeof(ez2DancerIOButtons[0]));
            static const int LIGHT_COUNT   = (int)(sizeof(lights)             / sizeof(lights[0]));
            s_bindings.load(settings, *s_mgr,
                            ioButtons,            IO_COUNT,
                            ez2DancerIOButtons,   DANCER_COUNT,
                            lights,               LIGHT_COUNT);
        } catch (...) {
            // Fall through to all-released state (no crash).
        }
    }

    // Apply early patches (after bindings load, before VEH registration).
    // Per CONTEXT.md apply order step 4: early patches fire here so they are
    // active before the VEH handler intercepts game I/O (e.g. keyboard re-enable).
    if (settingsLoaded) {
        std::string gameId = settings.gameSettings().value("game_id", "");
        if (!gameId.empty())
            settings.patchStore().applyPatches(gameId, /*applyEarly=*/true);
    }

    // Start background threads for input polling and light flushing.
    // VEH handler reads/writes only volatile caches — zero blocking on game thread.
    startInputPollingThread(s_bindings, *s_mgr);
    startLightFlushThread(s_bindings, *s_mgr);

    // Register VEH handler LAST — after all threads and caches are ready.
    AddVectoredExceptionHandler(1, CombinedIOHandler);

    // Wait for game init, then apply normal patches.
    int patchDelayMs = 2000;
    if (settingsLoaded)
        patchDelayMs = settings.globalSettings().value("patch_delay_ms", 2000);
    Sleep(patchDelayMs);

    if (settingsLoaded) {
        std::string gameId = settings.gameSettings().value("game_id", "");
        if (!gameId.empty())
            settings.patchStore().applyPatches(gameId, /*applyEarly=*/false);
    }

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        s_dllModule = hModule;
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    }
    if (reason == DLL_PROCESS_DETACH) {
        // Restore original refresh rate if we changed it.
        if (s_originalRefreshRate != 0 && s_originalRefreshRate != 60) {
            DEVMODEA dm = {};
            dm.dmSize = sizeof(dm);
            dm.dmDisplayFrequency = s_originalRefreshRate;
            dm.dmFields = DM_DISPLAYFREQUENCY;
            ChangeDisplaySettingsA(&dm, CDS_FULLSCREEN);
        }
    }
    return TRUE;
}
