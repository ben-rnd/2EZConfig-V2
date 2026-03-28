#include <windows.h>
#include <cstdint>

#include "ez2dancer_io.h"
#include "ez2dancer_io_input.h"
#include "ez2dancer_io_output.h"
#include "bindings.h"
#include "settings.h"
#include "logger.h"

static LONG WINAPI DancerIOHandler(PEXCEPTION_POINTERS ex) {
    if (ex->ExceptionRecord->ExceptionCode != EXCEPTION_PRIV_INSTRUCTION) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    auto* context = ex->ContextRecord;
    uint8_t* instructionPtr = reinterpret_cast<uint8_t*>(context->Eip);
    uint16_t port = static_cast<uint16_t>(context->Edx & 0xFFFF);
    uint8_t opcode = instructionPtr[0];
    int instructionLength = 1;

    // 0x66 prefix = 16-bit operand
    if (opcode == 0x66) {
        opcode = instructionPtr[1];
        instructionLength = 2;
    }

    switch (opcode) {
        case 0xED: { // IN AX, DX — 16-bit input
            uint16_t value;
            handleDancerIn(port, value);
            context->Eax = (context->Eax & 0xFFFF0000) | value;
            context->Eip += instructionLength;
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        case 0xEF: // OUT DX, AX — 16-bit output
            handleDancerOut(port, static_cast<uint16_t>(context->Eax & 0xFFFF));
            context->Eip += instructionLength;
            return EXCEPTION_CONTINUE_EXECUTION;

        default:
            return EXCEPTION_CONTINUE_SEARCH;
    }
}

void EZ2DancerIO::installHooks(SettingsManager* settings) {
    if (settings->globalSettings().value("io_emu", true)) {
        AddVectoredExceptionHandler(1, DancerIOHandler);
        Logger::info("[+] Dancer IO VEH installed (early)");
    }
}

void EZ2DancerIO::initialiseIO(BindingStore* bindings, InputManager* input,
                                SettingsManager* settings) {
    bool verbose = settings->gameSettings().value("verbose_output_logging", false);
    initDancerOutputLogging(verbose);

    startDancerInputPollThread(*bindings);
    startDancerLightFlushThread(*bindings);
}
