#include "dll_input.h"
#include "bindings.h"
#include "input_manager.h"
#include <windows.h>

// ---- Name arrays (must match strings.h exactly — these are JSON keys) ----

const char* const s_ioButtonNames[] = {
    "Test", "Service",
    "Effector 1", "Effector 2", "Effector 3", "Effector 4",
    "P1 Start", "P2 Start",
    "P1 1", "P1 2", "P1 3", "P1 4", "P1 5", "P1 Pedal",
    "P2 1", "P2 2", "P2 3", "P2 4", "P2 5", "P2 Pedal"
};
const int IO_BUTTON_COUNT = 20;

const char* const s_dancerButtonNames[] = {
    "Test", "Service",
    "P1 Left", "P1 Centre", "P1 Right",
    "P2 Left", "P2 Centre", "P2 Right",
    "P1 L Sensor Top", "P1 L Sesor Bottom",
    "P1 R Sensor Top", "P1 R Sesor Bottom",
    "P2 L Sensor Top", "P2 L Sesor Bottom",
    "P2 R Sensor Top", "P2 R Sesor Bottom"
};
const int DANCER_BUTTON_COUNT = 16;

const char* const s_lightNames[] = {
    "Effector 1", "Effector 2", "Effector 3", "Effector 4",
    "P1 Start", "P2 Start",
    "P1 Turntable",
    "P1 1", "P1 2", "P1 3", "P1 4", "P1 5",
    "P2 Turntable",
    "P2 1", "P2 2", "P2 3", "P2 4", "P2 5",
    "Neons", "Red Lamp L", "Red Lamp R", "Blue Lamp L", "Blue Lamp R"
};
const int LIGHT_NAME_COUNT = 23;

// ---- Stub implementations (filled in 03-02) ----

uint8_t computeDJPortByte(uint16_t port, const BindingStore& bs, InputManager& mgr) {
    (void)bs; (void)mgr;
    switch (port) {
        case 0x103: case 0x104: return 0x80;  // turntable center
        default: return 0xFF;                   // all buttons released
    }
}

uint16_t computeDancerPortWord(uint16_t port, const BindingStore& bs, InputManager& mgr) {
    (void)bs; (void)mgr;
    if (port == 0x304) return 0x0000;  // hands port default
    return 0xFFFF;                     // all released
}
