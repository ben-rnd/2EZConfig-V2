#include <windows.h>
#include <cstdint>

#include "ez2dj_io.h"
#include "ez2dj_io_input.h"
#include "ez2dj_io_output.h"
#include "bindings.h"
#include "settings.h"
#include "logger.h"

static LONG WINAPI DJIOHandler(PEXCEPTION_POINTERS ex) {
    if (ex->ExceptionRecord->ExceptionCode != EXCEPTION_PRIV_INSTRUCTION) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    auto* context = ex->ContextRecord;
    uint8_t* instructionPtr = reinterpret_cast<uint8_t*>(context->Eip);
    uint16_t port = static_cast<uint16_t>(context->Edx & 0xFFFF);
    uint8_t opcode = instructionPtr[0];

    switch (opcode) {
        case 0xEC: { // IN AL, DX — 8-bit input
            uint8_t value;
            handleDJIn(port, value);
            context->Eax = (context->Eax & 0xFFFFFF00) | value;
            context->Eip += 1;
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        case 0xEE: // OUT DX, AL — 8-bit output
            handleDJOut(port, static_cast<uint8_t>(context->Eax & 0xFF));
            context->Eip += 1;
            return EXCEPTION_CONTINUE_EXECUTION;

        default:
            return EXCEPTION_CONTINUE_SEARCH;
    }
}

void EZ2DJIO::installHooks(SettingsManager* settings) {
    if (settings->globalSettings().value("io_emu", true)) {
        AddVectoredExceptionHandler(1, DJIOHandler);
        Logger::info("[+] DJ IO VEH installed (early)");
    }
}

void EZ2DJIO::initialiseIO(BindingStore* bindings, InputManager* input,
                            SettingsManager* settings) {
    bool verbose = settings->gameSettings().value("verbose_output_logging", false);
    initDJOutputLogging(verbose);

    startDJInputPollThread(*bindings);
    startDJLightFlushThread(*bindings);
}
