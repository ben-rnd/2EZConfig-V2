#include <windows.h>
#include <mmsystem.h>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

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

static DWORD WINAPI InitThread(void*) {
    // Fix innacurate sleep()
    timeBeginPeriod(1);

    // Resolve DLL directory from the DLL's own module handle.
    char dir[MAX_PATH] = {};
    GetModuleFileNameA(s_dllModule, dir, MAX_PATH);
    char* lastSlash = strrchr(dir, '\\');
    if (lastSlash) *lastSlash = '\0';

    // Exit early if no settings file — obviously broken install if settings arent found.
    std::string settingsPath = std::string(dir) + "\\global-settings.json";
    if (GetFileAttributesA(settingsPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        return 0;

    // Read force_60hz early and install DDraw hook before game calls SetDisplayMode.
    bool force60hz = false;
    {
        std::ifstream f(settingsPath);
        if (f.is_open()) {
            try { force60hz = nlohmann::json::parse(f).value("force_60hz", false); }
            catch (...) {}
        }
    }
    installDDrawHook(force60hz);

    SettingsManager settings;
    try {
        settings.load(dir, dir);
    } catch (...) {
        return 0;
    }

    //Short sleep to fix crash when using legitimate data with dongles.
    Sleep(settings.globalSettings().value("shim_delay", 10));

    //Get game ID for patches
    std::string gameId = settings.gameSettings().value("game_id", "");

    // Apply early settings/patches that are time critical.
    if (settings.globalSettings().value("io_emu", true))
        AddVectoredExceptionHandler(1, IOHandler);

    if (settings.globalSettings().value("high_priority", false))
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    if (!gameId.empty())
        settings.patchStore().applyEarlyPatches(gameId);

    //Setup Input manager and load bindings
    s_mgr = new InputManager();

    static const int IO_COUNT     = (int)(sizeof(ioButtons)          / sizeof(ioButtons[0]));
    static const int DANCER_COUNT = (int)(sizeof(ez2DancerIOButtons) / sizeof(ez2DancerIOButtons[0]));
    static const int LIGHT_COUNT  = (int)(sizeof(lights)             / sizeof(lights[0]));
    try {
        s_bindings.load(settings, *s_mgr,
                        ioButtons,          IO_COUNT,
                        ez2DancerIOButtons, DANCER_COUNT,
                        lights,             LIGHT_COUNT);
    } catch (...) {}

    startInputPollingThread(s_bindings, *s_mgr);
    startLightFlushThread(s_bindings, *s_mgr);

    // Wait for game init, then apply standard patches.
    Sleep(settings.globalSettings().value("patch_delay_ms", 2000));
    settings.patchStore().applyVersionPatch("EZ2Config 1.00");
    if (!gameId.empty())
        settings.patchStore().applyPatches(gameId);

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        s_dllModule = hModule;
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    }
    (void)reason;
    return TRUE;
}
