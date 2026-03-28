#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <nlohmann/json.hpp>

extern "C" {
#include "io.hardlock.hooks.h"
#include "io.hardlock.emulator.h"
}

#include "ez2_io.h"
#include "ez2_io_input.h"
#include "ez2_io_output.h"
#include "bindings.h"
#include "input_manager.h"
#include "settings.h"
#include "game_defs.h"
#include "logger.h"
#include "utilities.h"

// --- VEH Exception Handler (intercepts IN/OUT opcodes for EZ2DJ/Dancer) ---

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

// --- Hardlock init (must run from DllMain before game code) ---

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

// --- Remember 1st resolution (must run from DllMain before patches) ---

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

// --- Public API ---

void EZ2IO::earlyInit(SettingsManager* settings, std::string& gameId) {
    resolveRemember1st(gameId);
    initHardlock(settings);
}

void EZ2IO::installHooks(BindingStore* bindings, InputManager* input,
                          SettingsManager* settings, const std::string& gameId) {
    // EZ2-specific logger config
    bool verbose = settings->gameSettings().value("verbose_output_logging", false);
    initOutputLogging(verbose);
    bool isDancer = familyFromGameId(gameId) == GameFamily::EZ2Dancer;
    initDancerOutput(isDancer);

    // VEH
    if (settings->globalSettings().value("io_emu", true)) {
        AddVectoredExceptionHandler(1, IOHandler);
        Logger::info("[+] IO Hook initialised");
    }

    // Start IO threads
    startInputPollThread(*bindings);
    startLightFlushThread(*bindings);
}
