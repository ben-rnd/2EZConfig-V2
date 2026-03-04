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

// ---- Pre-computed port cache ----
// VEH handler reads these directly — single volatile read, zero function calls.
// Background polling thread updates at ~1ms.

volatile uint8_t  s_djPortCache[7]     = { 0xFF, 0xFF, 0xFF, 0x80, 0x80, 0xFF, 0xFF };
volatile uint16_t s_dancerPortCache[4] = { 0xF000, 0xF000, 0x0000, 0x00FF };

// ---- SFINAE helper: detect if ButtonBinding has an 'alternatives' member ----

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

// ---- Port computation (called by polling thread, NOT by VEH handler) ----

static uint8_t computeDJPortByte(uint16_t port, const BindingStore& bs, InputManager& mgr) {
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
        case 0x103:  // P1 Turntable
            return bs.analogs[0].getPosition(mgr, mgr.getVttPosition(0));
        case 0x104:  // P2 Turntable
            return bs.analogs[1].getPosition(mgr, mgr.getVttPosition(1));
        default:
            return 0xFF;
    }
}

static uint16_t computeDancerPortWord(uint16_t port, const BindingStore& bs, InputManager& mgr) {
    switch (port) {
        case 0x300: {
            uint16_t output = 0x0FFF;
            if (isActionPressed(bs.dancerButtons[2],  mgr)) output &= 0x0FF0;  // P1 Left
            if (isActionPressed(bs.dancerButtons[3],  mgr)) output &= 0x0F0F;  // P1 Centre
            if (isActionPressed(bs.dancerButtons[4],  mgr)) output &= 0x00FF;  // P1 Right
            return static_cast<uint16_t>(~output);
        }
        case 0x302: {
            uint16_t output = 0x0FFF;
            if (isActionPressed(bs.dancerButtons[5],  mgr)) output &= 0x0FF0;  // P2 Left
            if (isActionPressed(bs.dancerButtons[6],  mgr)) output &= 0x0F0F;  // P2 Centre
            if (isActionPressed(bs.dancerButtons[7],  mgr)) output &= 0x00FF;  // P2 Right
            return static_cast<uint16_t>(~output);
        }
        case 0x304:
            return 0x0000;
        case 0x306: {
            uint16_t output = 0xFFFF;
            if (isActionPressed(bs.dancerButtons[8],  mgr)) output &= 0xF7FF;  // P1 L Top
            if (isActionPressed(bs.dancerButtons[9],  mgr)) output &= 0xEFFF;  // P1 L Bottom
            if (isActionPressed(bs.dancerButtons[10], mgr)) output &= 0xFBFF;  // P1 R Top
            if (isActionPressed(bs.dancerButtons[11], mgr)) output &= 0xDFFF;  // P1 R Bottom
            if (isActionPressed(bs.dancerButtons[12], mgr)) output &= 0xFDFF;  // P2 L Top
            if (isActionPressed(bs.dancerButtons[13], mgr)) output &= 0xBFFF;  // P2 L Bottom
            if (isActionPressed(bs.dancerButtons[14], mgr)) output &= 0xFEFF;  // P2 R Top
            if (isActionPressed(bs.dancerButtons[15], mgr)) output &= 0x7FFF;  // P2 R Bottom
            if (isActionPressed(bs.dancerButtons[0],  mgr)) output &= 0xFF20;  // Test
            if (isActionPressed(bs.dancerButtons[1],  mgr)) output &= 0xFF10;  // Service
            return output ^ 0xFF00;
        }
        default:
            return 0xFFFF;
    }
}

// ---- Background input polling thread ----
// Pre-computes all port values at ~1ms intervals.
// VEH handler becomes a single volatile array read.

struct InputPollArgs {
    const BindingStore* bs;
    InputManager* mgr;
};

static DWORD WINAPI inputPollingThread(void* arg) {
    auto* ctx = static_cast<InputPollArgs*>(arg);
    const BindingStore& bs = *ctx->bs;
    InputManager& mgr = *ctx->mgr;

    for (;;) {
        Sleep(1);

        // DJ ports
        s_djPortCache[1] = computeDJPortByte(0x101, bs, mgr);
        s_djPortCache[2] = computeDJPortByte(0x102, bs, mgr);
        s_djPortCache[3] = computeDJPortByte(0x103, bs, mgr);  // P1 TT
        s_djPortCache[4] = computeDJPortByte(0x104, bs, mgr);  // P2 TT
        s_djPortCache[6] = computeDJPortByte(0x106, bs, mgr);

        // Dancer ports
        s_dancerPortCache[0] = computeDancerPortWord(0x300, bs, mgr);
        s_dancerPortCache[1] = computeDancerPortWord(0x302, bs, mgr);
        // s_dancerPortCache[2] = 0x0000 always (port 0x304)
        s_dancerPortCache[3] = computeDancerPortWord(0x306, bs, mgr);
    }
    return 0;
}

void startInputPollingThread(const BindingStore& bs, InputManager& mgr) {
    auto* ctx = new InputPollArgs{ &bs, &mgr };
    CreateThread(nullptr, 0, inputPollingThread, ctx, 0, nullptr);
}
