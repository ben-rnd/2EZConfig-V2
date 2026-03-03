#include <windows.h>
#include <stdint.h>

// Intercepts the EZ2DJ/EZ2DANCER game's IN/OUT port instructions.
// The game talks to its ISA IO board via x86 IN/OUT instructions which trigger
// EXCEPTION_PRIV_INSTRUCTION on modern Windows or when userport is disabled on WinXP.
// We catch those and simulate the hardware responses.

static LONG WINAPI DjIOportHandler(PEXCEPTION_POINTERS ex) {
    if (ex->ExceptionRecord->ExceptionCode != EXCEPTION_PRIV_INSTRUCTION)
        return EXCEPTION_CONTINUE_SEARCH;

    uint8_t opcode = *(uint8_t*)ex->ContextRecord->Eip & 0xFF;
    uint16_t port  = ex->ContextRecord->Edx & 0xFFFF;

    // IN AL, DX  (0xEC) — game reading from IO port
    if (opcode == 0xEC) {
        switch (port) {
            case 0x101: // TEST SVC E4 E3 E2 E1 P2Start P1Start
            case 0x102: // P1 Pedal B5 B4 B3 B2 B1
            case 0x106: // P2 Pedal B10 B9 B8 B7 B6
                ex->ContextRecord->Eax = 0xFF;  // no buttons pressed
                break;
            case 0x103: // P1 Turntable (0-255)
            case 0x104: // P2 Turntable (0-255)
                ex->ContextRecord->Eax = 0x80;  // turntable center
                break;
            default:
                ex->ContextRecord->Eax = 0xFF;
                break;
        }
        ex->ContextRecord->Eip++;
        return EXCEPTION_CONTINUE_EXECUTION;
    } else if (opcode == 0xEE) {    // OUT DX, AL  (lights)
        // TODO: forward to lighting output
        ex->ContextRecord->Eip++;
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI DancerIOportHandler(PEXCEPTION_POINTERS ex) {
        uint16_t opcode = *(uint8_t*)ex->ContextRecord->Eip & 0xFFFF;
        uint16_t port = ex->ContextRecord->Edx & 0xFFFF;
        // Game Input
        if (opcode == 0xED66) {
            switch (port) {
                case 0x300: //Feet -> 1P-RIGHT(TEST)(4bits), 1P-BACK(SVC)(4bits), 1P-LEFT(E4)(4bits), Credit count? - how weird
                    ex->ContextRecord->Eax = 0xFFFF;
                    break;
                case 0x302: //Feet -> 2P-RIGHT(E2)(4bits) 2P-BACK(E1)(4bits)  2P-RIGHT(P2)(4bits) BLANK(P1)
                    ex->ContextRecord->Eax = 0xFFFF;
                    break;
                case 0x304: //Hands 
                    ex->ContextRecord->Eax = 0;
                    break;
                case 0x306: //HANDS AND Service TESTING INPUTS B4 B3 B2 B1
                    ex->ContextRecord->Eax = 0xFFFF;
                    break;
                default:
                    ex->ContextRecord->Eax = 0xFFFF;
                    break;
            }
            // Skip over the instruction that caused the exception:
            ex->ContextRecord->Eip += 2;
        }

        // Game output (lights)
        else if (opcode == 0xEF66) {
            switch (port) {
            default:
                ex->ContextRecord->Eax = 0;
                break;
            }
            // Skip over the instruction that caused the exception:
                ex->ContextRecord->Eip += 2;
        } 

    return EXCEPTION_CONTINUE_EXECUTION;
}

static DWORD WINAPI PatchThread(void*) {
    Sleep(10);
    SetUnhandledExceptionFilter(DjIOportHandler);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        CreateThread(NULL, 0, PatchThread, NULL, 0, NULL);
    return TRUE;
}
