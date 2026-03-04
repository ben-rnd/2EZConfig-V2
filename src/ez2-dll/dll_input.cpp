#include "dll_input.h"
#include "bindings.h"
#include "input_manager.h"
#include <windows.h>
#include <type_traits>

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

// ---- SFINAE helper: detect if ButtonBinding has an 'alternatives' member ----
// Phase 2.1 adds std::vector<ButtonBinding> alternatives to ButtonBinding.
// This allows the OR logic to activate automatically when the field is present.

template<typename T, typename = void>
struct has_alternatives : std::false_type {};
template<typename T>
struct has_alternatives<T, std::void_t<decltype(std::declval<T>().alternatives)>> : std::true_type {};

// ---- Returns true if a single ButtonBinding is currently pressed ----
static bool isPressed(const ButtonBinding& b, InputManager& mgr) {
    if (!b.isSet()) return false;
    if (b.isKeyboard()) return (GetAsyncKeyState(b.vk_code) & 0x8000) != 0;
    return mgr.getButtonState(b.device_path, b.button_idx);
}

// ---- Returns true if primary OR any alternative binding is pressed ----
// OR logic over alternatives activates automatically when field exists (SFINAE).
// Must be a template so the if constexpr branch body is truly dependent and
// not instantiated when has_alternatives<B>::value is false.
template<typename B = ButtonBinding>
static bool isActionPressed(const B& primary, InputManager& mgr) {
    if (isPressed(primary, mgr)) return true;
    if constexpr (has_alternatives<B>::value) {
        for (const auto& alt : primary.alternatives) {
            if (isPressed(alt, mgr)) return true;
        }
    }
    return false;
}

// ---- computeDJPortByte ----
// Active-low: bit=0 when pressed, bit=1 when released.
// Start with 0xFF (all released), clear bits for pressed actions.
// Unused bits (5,6 on 0x102/0x106) remain 1 per ISA hardware spec.

uint8_t computeDJPortByte(uint16_t port, const BindingStore& bs, InputManager& mgr) {
    uint8_t result = 0xFF;
    switch (port) {
        case 0x101:
            if (isActionPressed(bs.buttons[6],  mgr)) result &= ~(1 << 0);  // P1 Start
            if (isActionPressed(bs.buttons[7],  mgr)) result &= ~(1 << 1);  // P2 Start
            if (isActionPressed(bs.buttons[2],  mgr)) result &= ~(1 << 2);  // Effector 1
            if (isActionPressed(bs.buttons[3],  mgr)) result &= ~(1 << 3);  // Effector 2
            if (isActionPressed(bs.buttons[4],  mgr)) result &= ~(1 << 4);  // Effector 3
            if (isActionPressed(bs.buttons[5],  mgr)) result &= ~(1 << 5);  // Effector 4
            if (isActionPressed(bs.buttons[1],  mgr)) result &= ~(1 << 6);  // Service
            if (isActionPressed(bs.buttons[0],  mgr)) result &= ~(1 << 7);  // Test
            return result;
        case 0x102:
            if (isActionPressed(bs.buttons[8],  mgr)) result &= ~(1 << 0);  // P1 1
            if (isActionPressed(bs.buttons[9],  mgr)) result &= ~(1 << 1);  // P1 2
            if (isActionPressed(bs.buttons[10], mgr)) result &= ~(1 << 2);  // P1 3
            if (isActionPressed(bs.buttons[11], mgr)) result &= ~(1 << 3);  // P1 4
            if (isActionPressed(bs.buttons[12], mgr)) result &= ~(1 << 4);  // P1 5
            // bits 5,6: unused — remain 1 per ISA spec
            if (isActionPressed(bs.buttons[13], mgr)) result &= ~(1 << 7);  // P1 Pedal
            return result;
        case 0x106:
            if (isActionPressed(bs.buttons[14], mgr)) result &= ~(1 << 0);  // P2 1
            if (isActionPressed(bs.buttons[15], mgr)) result &= ~(1 << 1);  // P2 2
            if (isActionPressed(bs.buttons[16], mgr)) result &= ~(1 << 2);  // P2 3
            if (isActionPressed(bs.buttons[17], mgr)) result &= ~(1 << 3);  // P2 4
            if (isActionPressed(bs.buttons[18], mgr)) result &= ~(1 << 4);  // P2 5
            // bits 5,6: unused — remain 1 per ISA spec
            if (isActionPressed(bs.buttons[19], mgr)) result &= ~(1 << 7);  // P2 Pedal
            return result;
        case 0x103:  // P1 Turntable (analog, 0-255, center=0x80)
            return bs.analogs[0].getPosition(mgr, mgr.getVttPosition(0));
        case 0x104:  // P2 Turntable (analog, 0-255, center=0x80)
            return bs.analogs[1].getPosition(mgr, mgr.getVttPosition(1));
        default:
            return 0xFF;  // unrecognized port — all released
    }
}

// ---- computeDancerPortWord ----
// Dancer port mapping is LOW confidence — best-effort from dll.cpp comments.
// Ports 0x300/0x302/0x306 are active-low (0xFFFF default, bits cleared on press).
// Port 0x304 (hand sensors) is active-high (0x0000 default, bits set on press).
// This lookup table can be corrected per-bit when tested against real hardware.

uint16_t computeDancerPortWord(uint16_t port, const BindingStore& bs, InputManager& mgr) {
    uint16_t result = 0xFFFF;
    switch (port) {
        case 0x300:
            // P1 foot panels + Test/Service (active-low)
            if (isActionPressed(bs.dancerButtons[0],  mgr)) result &= ~(1 << 0);  // Test
            if (isActionPressed(bs.dancerButtons[1],  mgr)) result &= ~(1 << 1);  // Service
            if (isActionPressed(bs.dancerButtons[2],  mgr)) result &= ~(1 << 2);  // P1 Left
            if (isActionPressed(bs.dancerButtons[3],  mgr)) result &= ~(1 << 3);  // P1 Centre
            if (isActionPressed(bs.dancerButtons[4],  mgr)) result &= ~(1 << 4);  // P1 Right
            return result;
        case 0x302:
            // P2 foot panels (active-low)
            if (isActionPressed(bs.dancerButtons[5],  mgr)) result &= ~(1 << 0);  // P2 Left
            if (isActionPressed(bs.dancerButtons[6],  mgr)) result &= ~(1 << 1);  // P2 Centre
            if (isActionPressed(bs.dancerButtons[7],  mgr)) result &= ~(1 << 2);  // P2 Right
            return result;
        case 0x304:
            // Hand sensors (active-high per existing code convention)
            result = 0x0000;
            if (isActionPressed(bs.dancerButtons[8],  mgr)) result |= (1 << 0);   // P1 L Sensor Top
            if (isActionPressed(bs.dancerButtons[9],  mgr)) result |= (1 << 1);   // P1 L Sensor Bottom
            if (isActionPressed(bs.dancerButtons[10], mgr)) result |= (1 << 2);   // P1 R Sensor Top
            if (isActionPressed(bs.dancerButtons[11], mgr)) result |= (1 << 3);   // P1 R Sensor Bottom
            if (isActionPressed(bs.dancerButtons[12], mgr)) result |= (1 << 4);   // P2 L Sensor Top
            if (isActionPressed(bs.dancerButtons[13], mgr)) result |= (1 << 5);   // P2 L Sensor Bottom
            if (isActionPressed(bs.dancerButtons[14], mgr)) result |= (1 << 6);   // P2 R Sensor Top
            if (isActionPressed(bs.dancerButtons[15], mgr)) result |= (1 << 7);   // P2 R Sensor Bottom
            return result;
        case 0x306:
            // Testing inputs — mirrors Test/Service from 0x300 (active-low)
            if (isActionPressed(bs.dancerButtons[0],  mgr)) result &= ~(1 << 0);  // Test
            if (isActionPressed(bs.dancerButtons[1],  mgr)) result &= ~(1 << 1);  // Service
            return result;
        default:
            return 0xFFFF;  // unrecognized port — all released
    }
}
