#include "ez2_io_output.h"
#include "bindings.h"
#include "input_manager.h"
#include "game_defs.h"
#include "logger.h"
#include "utilities.h"
#include <windows.h>
#include <atomic>

static constexpr int MAX_LIGHT_COUNT = BindingStore::DANCER_LIGHT_COUNT; // 24 (dj) or 7 (dancer cabinet only)
static float s_lightState[MAX_LIGHT_COUNT] = {};
static std::atomic<bool> s_lightDirty{ false };
static std::atomic<bool> s_verboseOutput{ false };
static bool s_isDancer = false;

void initOutputLogging(bool verbose) {
    s_verboseOutput.store(verbose);
}

void initDancerOutput(bool isDancer) {
    s_isDancer = isDancer;
}

// Each entry maps a light channel to the bit that controls it within a port.

struct LightBit {
    int light;
    uint8_t mask;
};

// Port 0x100: lamps + neons
static const LightBit djLamps[] = {
    { (int)Light::RED_LAMP_L,  0x01 },
    { (int)Light::RED_LAMP_R,  0x02 },
    { (int)Light::BLUE_LAMP_L, 0x04 },
    { (int)Light::BLUE_LAMP_R, 0x08 },
    { (int)Light::NEONS,       0x10 },
};

// Port 0x101: starts + effectors
static const LightBit djStartsEffectors[] = {
    { (int)Light::P1_START,   0x01 },
    { (int)Light::P2_START,   0x02 },
    { (int)Light::EFFECTOR_1, 0x04 },
    { (int)Light::EFFECTOR_2, 0x08 },
    { (int)Light::EFFECTOR_3, 0x10 },
    { (int)Light::EFFECTOR_4, 0x20 },
};

// Port 0x102: P1 buttons + turntable
static const LightBit djP1Lights[] = {
    { (int)Light::P1_1,         0x01 },
    { (int)Light::P1_2,         0x02 },
    { (int)Light::P1_3,         0x04 },
    { (int)Light::P1_4,         0x08 },
    { (int)Light::P1_5,         0x10 },
    { (int)Light::P1_TURNTABLE, 0x20 },
};

// Port 0x103: P2 buttons + turntable
static const LightBit djP2Lights[] = {
    { (int)Light::P2_1,         0x01 },
    { (int)Light::P2_2,         0x02 },
    { (int)Light::P2_3,         0x04 },
    { (int)Light::P2_4,         0x08 },
    { (int)Light::P2_5,         0x10 },
    { (int)Light::P2_TURNTABLE, 0x20 },
};

static void applyLights(uint8_t value, const LightBit* table, size_t count) {
    for (size_t i = 0; i < count; i++) {
        s_lightState[table[i].light] = (value & table[i].mask) ? 1.0f : 0.0f;
    }
}

void handleDJOut(uint16_t port, uint8_t value) {
    switch (port) {
        case 0x100: applyLights(value, djLamps,           std::size(djLamps));           break;
        case 0x101: applyLights(value, djStartsEffectors, std::size(djStartsEffectors)); break;
        case 0x102: applyLights(value, djP1Lights,        std::size(djP1Lights));        break;
        case 0x103: applyLights(value, djP2Lights,        std::size(djP2Lights));        break;
        default:
            if (s_verboseOutput.load(std::memory_order_relaxed)) {
                Logger::warn("[IO] Unhandled DJ port write: 0x" + toHexString(port) + " value: 0b" + toBinaryString(value));
            } else {
                Logger::warnOnce("[IO] Unexpected DJ port write: 0x" + toHexString(port));
            }
            return;
    }
    if (s_verboseOutput.load(std::memory_order_relaxed)) {
        Logger::warn("[IO] DJ port write: 0x" + toHexString(port) + " value: 0b" + toBinaryString(value));
    }
    s_lightDirty.store(true);
}

// 0x30A cabinet lights: 16-bit, active-low, simple bit-mapped
struct LightBit16 {
    int light;
    uint16_t mask;
};

static const LightBit16 dancerCabinetLights[] = {
    { (int)DancerLight::NEON,                0x0004 }, // b2
    { (int)DancerLight::LIGHT_LEFT_TOP,      0x0100 }, // b8
    { (int)DancerLight::LIGHT_LEFT_MIDDLE,   0x0200 }, // b9
    { (int)DancerLight::LIGHT_LEFT_BOTTOM,   0x0400 }, // b10
    { (int)DancerLight::LIGHT_RIGHT_TOP,     0x0800 }, // b11
    { (int)DancerLight::LIGHT_RIGHT_MIDDLE,  0x0001 }, // b0
    { (int)DancerLight::LIGHT_RIGHT_BOTTOM,  0x0002 }, // b1
};

static void applyLightsInverted(uint16_t value, const LightBit16* table, size_t count) {
    for (size_t i = 0; i < count; i++) {
        s_lightState[table[i].light] = (value & table[i].mask) ? 0.0f : 1.0f;
    }
}

void handleDancerOut(uint16_t port, uint16_t value) {
    switch (port) {
        case 0x30A:
            applyLightsInverted(value, dancerCabinetLights, std::size(dancerCabinetLights));
            break;

        // 0x30C (Hand Sensor LEDs) and 0x308 (Pads). I suspect there are managed via sub I/O
        // with 'state" commands sent from the game. May need to emulate an approximation
        // once more footage of the cab is supplied.
        // Requires further research. Log only for now.
        case 0x30C:
        case 0x308:
        default:
            if (s_verboseOutput.load(std::memory_order_relaxed)) {
                Logger::warn("[IO] Unhandled dancer port write: 0x" + toHexString(port) + " value: 0b" + toBinaryString(value));
            } else {
                Logger::warnOnce("[IO] Unexpected dancer port write: 0x" + toHexString(port));
            }
            return;
    }
    if (s_verboseOutput.load(std::memory_order_relaxed)) {
        Logger::warn("[IO] dancer port write: 0x" + toHexString(port) + " value: 0b" + toBinaryString(value));
    }
    s_lightDirty.store(true);
}

static DWORD WINAPI lightFlushThread(void* bindingsPtr) {
    const BindingStore& bindings = *static_cast<const BindingStore*>(bindingsPtr);

    while (true) {
        Sleep(1);
        if (!s_lightDirty.load()) {
            continue;
        }
        s_lightDirty.store(false);

        if (s_isDancer) {
            for (int i = 0; i < BindingStore::DANCER_LIGHT_COUNT; ++i) {
                const LightBinding& lb = bindings.dancerLights[i];
                if (!lb.isSet()) continue;
                bindings.mgr->setLight(lb.devicePath, lb.outputIdx, s_lightState[i]);
            }
        } else {
            for (int i = 0; i < BindingStore::LIGHT_COUNT; ++i) {
                const LightBinding& lb = bindings.lights[i];
                if (!lb.isSet()) continue;
                bindings.mgr->setLight(lb.devicePath, lb.outputIdx, s_lightState[i]);
            }
        }
    }
    return 0;
}

void startLightFlushThread(const BindingStore& bindings) {
    Logger::info("[Output] Light thread started");
    CreateThread(nullptr, 0, lightFlushThread, const_cast<BindingStore*>(&bindings), 0, nullptr);
}
