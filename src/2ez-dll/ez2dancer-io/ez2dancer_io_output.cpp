#include "ez2dancer_io_output.h"
#include "bindings.h"
#include "input_manager.h"
#include "game_defs.h"
#include "logger.h"
#include "utilities.h"
#include <windows.h>
#include <atomic>

static float s_lightState[static_cast<int>(DancerLight::COUNT)] = {};
static std::atomic<bool> s_lightDirty{ false };
static std::atomic<bool> s_verboseOutput{ false };

void initDancerOutputLogging(bool verbose) {
    s_verboseOutput.store(verbose);
}

struct LightBit16 {
    int light;
    uint16_t mask;
};

// 0x30A cabinet lights: 16-bit, active-low, simple bit-mapped
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

        // 0x30C (Hand Sensor LEDs) and 0x308 (Pads). Requires further research.
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
        if (!s_lightDirty.load()) continue;
        s_lightDirty.store(false);
        for (int i = 0; i < BindingStore::DANCER_LIGHT_COUNT; ++i) {
            const LightBinding& lb = bindings.dancerLights[i];
            if (!lb.isSet()) continue;
            bindings.mgr->setLight(lb.devicePath, lb.outputIdx, s_lightState[i]);
        }
    }
    return 0;
}

void startDancerLightFlushThread(const BindingStore& bindings) {
    Logger::info("[Dancer Output] Light thread started");
    CreateThread(nullptr, 0, lightFlushThread, const_cast<BindingStore*>(&bindings), 0, nullptr);
}
