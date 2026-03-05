#include "dll_input.h"
#include "bindings.h"
#include "input_manager.h"
#include <windows.h>
#include <type_traits>

// Name arrays — must match strings.h exactly (these are JSON keys).

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

// DJ button indices into BindingStore::buttons[] — matches s_ioButtonNames[].
enum DJButton {
    DJ_TEST, DJ_SERVICE,
    DJ_EFFECTOR_1, DJ_EFFECTOR_2, DJ_EFFECTOR_3, DJ_EFFECTOR_4,
    DJ_P1_START, DJ_P2_START,
    DJ_P1_1, DJ_P1_2, DJ_P1_3, DJ_P1_4, DJ_P1_5, DJ_P1_PEDAL,
    DJ_P2_1, DJ_P2_2, DJ_P2_3, DJ_P2_4, DJ_P2_5, DJ_P2_PEDAL
};

// Dancer button indices into BindingStore::dancerButtons[] — matches s_dancerButtonNames[].
enum DancerButton {
    DANCER_TEST, DANCER_SERVICE,
    DANCER_P1_LEFT, DANCER_P1_CENTRE, DANCER_P1_RIGHT,
    DANCER_P2_LEFT, DANCER_P2_CENTRE, DANCER_P2_RIGHT,
    DANCER_P1_L_SENSOR_TOP, DANCER_P1_L_SENSOR_BOTTOM,
    DANCER_P1_R_SENSOR_TOP, DANCER_P1_R_SENSOR_BOTTOM,
    DANCER_P2_L_SENSOR_TOP, DANCER_P2_L_SENSOR_BOTTOM,
    DANCER_P2_R_SENSOR_TOP, DANCER_P2_R_SENSOR_BOTTOM
};

// DJ IN button ports — bit-to-button mapping (active-low).
// -1 = unused bit (stays high / released).
static const int s_djInMap[3][8] = {
    // Port 0x101
    { DJ_P1_START, DJ_P2_START, DJ_EFFECTOR_1, DJ_EFFECTOR_2, DJ_EFFECTOR_3, DJ_EFFECTOR_4, DJ_SERVICE, DJ_TEST },
    // Port 0x102
    { DJ_P1_1, DJ_P1_2, DJ_P1_3, DJ_P1_4, DJ_P1_5, -1, -1, DJ_P1_PEDAL },
    // Port 0x106
    { DJ_P2_1, DJ_P2_2, DJ_P2_3, DJ_P2_4, DJ_P2_5, -1, -1, DJ_P2_PEDAL },
};

// Dancer foot panels per port — 3 panels, each clears a 4-bit nibble.
static const int s_dancerFeetP1[] = { DANCER_P1_LEFT, DANCER_P1_CENTRE, DANCER_P1_RIGHT };
static const int s_dancerFeetP2[] = { DANCER_P2_LEFT, DANCER_P2_CENTRE, DANCER_P2_RIGHT };

// Dancer hand sensors — maps sensor index to bit position in port 0x306.
static const int s_handSensorBit[8] = { 11, 12, 10, 13, 9, 14, 8, 15 };

// Pre-computed port cache. VEH handler reads from here (single volatile read).
volatile uint8_t  s_djPortCache[7]     = { 0xFF, 0xFF, 0xFF, 0x80, 0x80, 0xFF, 0xFF };
volatile uint16_t s_dancerPortCache[4] = { 0xF000, 0xF000, 0x0000, 0x00FF };

// SFINAE: detect if ButtonBinding has an 'alternatives' member (added by Phase 2.1).
template<typename T, typename = void>
struct has_alternatives : std::false_type {};
template<typename T>
struct has_alternatives<T, std::void_t<decltype(std::declval<T>().alternatives)>> : std::true_type {};

static bool isPressed(const ButtonBinding& b, InputManager& mgr) {
    if (!b.isSet()) return false;
    if (b.isKeyboard()) return (GetAsyncKeyState(b.vk_code) & 0x8000) != 0;
    return mgr.getButtonState(b.device_path, b.button_idx);
}

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

static uint8_t computeButtonPort(const int map[8], const BindingStore& bs, InputManager& mgr) {
    uint8_t result = 0xFF;
    for (int bit = 0; bit < 8; ++bit) {
        if (map[bit] >= 0 && isActionPressed(bs.buttons[map[bit]], mgr))
            result &= ~(1 << bit);
    }
    return result;
}

static uint16_t computeFeetPort(const int panels[3], const BindingStore& bs, InputManager& mgr) {
    uint16_t output = 0x0FFF;
    for (int i = 0; i < 3; ++i) {
        if (isActionPressed(bs.dancerButtons[panels[i]], mgr))
            output &= ~(0xF << (i * 4));
    }
    return static_cast<uint16_t>(~output);
}

static uint16_t computeHandsPort(const BindingStore& bs, InputManager& mgr) {
    uint16_t output = 0xFFFF;

    for (int i = 0; i < 8; ++i) {
        if (isActionPressed(bs.dancerButtons[DANCER_P1_L_SENSOR_TOP + i], mgr))
            output &= ~(1 << s_handSensorBit[i]);
    }

    static const uint16_t TEST_MASK    = 0xFF00 | (1 << 5);
    static const uint16_t SERVICE_MASK = 0xFF00 | (1 << 4);
    if (isActionPressed(bs.dancerButtons[DANCER_TEST],    mgr)) output &= TEST_MASK;
    if (isActionPressed(bs.dancerButtons[DANCER_SERVICE], mgr)) output &= SERVICE_MASK;

    return output ^ 0xFF00;
}

static uint8_t computeDJPortByte(uint16_t port, const BindingStore& bs, InputManager& mgr) {
    switch (port) {
        case 0x101: return computeButtonPort(s_djInMap[0], bs, mgr);
        case 0x102: return computeButtonPort(s_djInMap[1], bs, mgr);
        case 0x106: return computeButtonPort(s_djInMap[2], bs, mgr);
        case 0x103: return bs.analogs[0].getPosition(mgr, mgr.getVttPosition(0));
        case 0x104: return bs.analogs[1].getPosition(mgr, mgr.getVttPosition(1));
        default:    return 0xFF;
    }
}

static uint16_t computeDancerPortWord(uint16_t port, const BindingStore& bs, InputManager& mgr) {
    switch (port) {
        case 0x300: return computeFeetPort(s_dancerFeetP1, bs, mgr);
        case 0x302: return computeFeetPort(s_dancerFeetP2, bs, mgr);
        case 0x304: return 0x0000;
        case 0x306: return computeHandsPort(bs, mgr);
        default:    return 0xFFFF;
    }
}

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

        s_djPortCache[1] = computeDJPortByte(0x101, bs, mgr);
        s_djPortCache[2] = computeDJPortByte(0x102, bs, mgr);
        s_djPortCache[3] = computeDJPortByte(0x103, bs, mgr);
        s_djPortCache[4] = computeDJPortByte(0x104, bs, mgr);
        s_djPortCache[6] = computeDJPortByte(0x106, bs, mgr);

        s_dancerPortCache[0] = computeDancerPortWord(0x300, bs, mgr);
        s_dancerPortCache[1] = computeDancerPortWord(0x302, bs, mgr);
        s_dancerPortCache[3] = computeDancerPortWord(0x306, bs, mgr);
    }
    return 0;
}

void startInputPollingThread(const BindingStore& bs, InputManager& mgr) {
    auto* ctx = new InputPollArgs{ &bs, &mgr };
    CreateThread(nullptr, 0, inputPollingThread, ctx, 0, nullptr);
}
