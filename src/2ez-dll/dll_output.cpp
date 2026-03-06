#include "dll_output.h"
#include "bindings.h"
#include "input_manager.h"
#include "strings.h"
#include <windows.h>


static volatile float s_lightState[BindingStore::LIGHT_COUNT] = {};
static volatile bool  s_lightDirty = false;

static void setLight(int channel, bool on) {
    s_lightState[channel] = on ? 1.0f : 0.0f;
}

void handleDJOut(uint16_t port, uint8_t value, const BindingStore& bs) {
    (void)bs;
    switch (port) {
        case 0x100: // Lamps + Neons
            setLight(LIGHT_RED_LAMP_L,   value & 0x01);
            setLight(LIGHT_RED_LAMP_R,   value & 0x02);
            setLight(LIGHT_BLUE_LAMP_L,  value & 0x04);
            setLight(LIGHT_BLUE_LAMP_R,  value & 0x08);
            setLight(LIGHT_NEONS,        value & 0x10);
            break;
        case 0x101: // Starts + Effectors
            setLight(LIGHT_P1_START,     value & 0x01);
            setLight(LIGHT_P2_START,     value & 0x02);
            setLight(LIGHT_EFFECTOR_1,   value & 0x04);
            setLight(LIGHT_EFFECTOR_2,   value & 0x08);
            setLight(LIGHT_EFFECTOR_3,   value & 0x10);
            setLight(LIGHT_EFFECTOR_4,   value & 0x20);
            break;
        case 0x102: // P1 Buttons + Turntable
            setLight(LIGHT_P1_1,         value & 0x01);
            setLight(LIGHT_P1_2,         value & 0x02);
            setLight(LIGHT_P1_3,         value & 0x04);
            setLight(LIGHT_P1_4,         value & 0x08);
            setLight(LIGHT_P1_5,         value & 0x10);
            setLight(LIGHT_P1_TURNTABLE, value & 0x20);
            break;
        case 0x103: // P2 Buttons + Turntable
            setLight(LIGHT_P2_1,         value & 0x01);
            setLight(LIGHT_P2_2,         value & 0x02);
            setLight(LIGHT_P2_3,         value & 0x04);
            setLight(LIGHT_P2_4,         value & 0x08);
            setLight(LIGHT_P2_5,         value & 0x10);
            setLight(LIGHT_P2_TURNTABLE, value & 0x20);
            break;
        default:
            return;
    }
    s_lightDirty = true;
}

void handleDancerOut(uint16_t port, uint8_t value, const BindingStore& bs) {
    (void)port; (void)value; (void)bs;
}

struct LightFlushArgs {
    const BindingStore* bs;
    InputManager* mgr;
};

static DWORD WINAPI lightFlushThread(void* arg) {
    auto* ctx = static_cast<LightFlushArgs*>(arg);
    const BindingStore& bs = *ctx->bs;
    InputManager& mgr = *ctx->mgr;

    while (true) {
        Sleep(1);
        if (!s_lightDirty) continue;
        s_lightDirty = false;

        for (int i = 0; i < BindingStore::LIGHT_COUNT; ++i) {
            const LightBinding& lb = bs.lights[i];
            if (!lb.isSet()) continue;
            mgr.setLight(lb.device_path, lb.output_idx, s_lightState[i]);
        }
    }
    return 0;
}

void startLightFlushThread(const BindingStore& bs, InputManager& mgr) {
    auto* ctx = new LightFlushArgs{ &bs, &mgr };
    CreateThread(nullptr, 0, lightFlushThread, ctx, 0, nullptr);
}
