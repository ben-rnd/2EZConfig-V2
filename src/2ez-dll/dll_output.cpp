#include "dll_output.h"
#include "bindings.h"
#include "input_manager.h"
#include "game_defs.h"
#include "logger.h"
#include <windows.h>
#include <atomic>

static float s_lightState[BindingStore::LIGHT_COUNT] = {};
static std::atomic<bool> s_lightDirty{ false };

static void setLight(int channel, bool on) {
    s_lightState[channel] = on ? 1.0f : 0.0f;
}

void handleDJOut(uint16_t port, uint8_t value) {
    switch (port) {
        case 0x100: // Lamps + Neons
            setLight((int)Light::RED_LAMP_L,   value & 0x01);
            setLight((int)Light::RED_LAMP_R,   value & 0x02);
            setLight((int)Light::BLUE_LAMP_L,  value & 0x04);
            setLight((int)Light::BLUE_LAMP_R,  value & 0x08);
            setLight((int)Light::NEONS,        value & 0x10);
            break;
        case 0x101: // Starts + Effectors
            setLight((int)Light::P1_START,     value & 0x01);
            setLight((int)Light::P2_START,     value & 0x02);
            setLight((int)Light::EFFECTOR_1,   value & 0x04);
            setLight((int)Light::EFFECTOR_2,   value & 0x08);
            setLight((int)Light::EFFECTOR_3,   value & 0x10);
            setLight((int)Light::EFFECTOR_4,   value & 0x20);
            break;
        case 0x102: // P1 Buttons + Turntable
            setLight((int)Light::P1_1,         value & 0x01);
            setLight((int)Light::P1_2,         value & 0x02);
            setLight((int)Light::P1_3,         value & 0x04);
            setLight((int)Light::P1_4,         value & 0x08);
            setLight((int)Light::P1_5,         value & 0x10);
            setLight((int)Light::P1_TURNTABLE, value & 0x20);
            break;
        case 0x103: // P2 Buttons + Turntable
            setLight((int)Light::P2_1,         value & 0x01);
            setLight((int)Light::P2_2,         value & 0x02);
            setLight((int)Light::P2_3,         value & 0x04);
            setLight((int)Light::P2_4,         value & 0x08);
            setLight((int)Light::P2_5,         value & 0x10);
            setLight((int)Light::P2_TURNTABLE, value & 0x20);
            break;
        default:
            return;
    }
    s_lightDirty.store(true);
}

void handleDancerOut(uint16_t port, uint8_t value) {
    //TODO
}

static DWORD WINAPI lightFlushThread(void* arg) {
    const BindingStore& bindings = *static_cast<const BindingStore*>(arg);

    while (true) {
        Sleep(1);
        if (!s_lightDirty.load()) continue;
        s_lightDirty.store(false);

        for (int i = 0; i < BindingStore::LIGHT_COUNT; ++i) {
            const LightBinding& lb = bindings.lights[i];
            if (!lb.isSet()) continue;
            bindings.mgr->setLight(lb.device_path, lb.output_idx, s_lightState[i]);
        }
    }
    return 0;
}

void startLightFlushThread(const BindingStore& bindings) {
    Logger::info("[Output] Light flush thread started");
    CreateThread(nullptr, 0, lightFlushThread, const_cast<BindingStore*>(&bindings), 0, nullptr);
}
