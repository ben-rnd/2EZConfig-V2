#include "dll_output.h"
#include "bindings.h"
#include "input_manager.h"
#include <windows.h>

// Light indices into BindingStore::lights[] — matches s_lightNames[] order.
enum LightChannel {
    LIGHT_EFFECTOR_1, LIGHT_EFFECTOR_2, LIGHT_EFFECTOR_3, LIGHT_EFFECTOR_4,
    LIGHT_P1_START, LIGHT_P2_START,
    LIGHT_P1_TURNTABLE,
    LIGHT_P1_1, LIGHT_P1_2, LIGHT_P1_3, LIGHT_P1_4, LIGHT_P1_5,
    LIGHT_P2_TURNTABLE,
    LIGHT_P2_1, LIGHT_P2_2, LIGHT_P2_3, LIGHT_P2_4, LIGHT_P2_5,
    LIGHT_NEONS,
    LIGHT_RED_LAMP_L, LIGHT_RED_LAMP_R, LIGHT_BLUE_LAMP_L, LIGHT_BLUE_LAMP_R
};

// DJ OUT port bit-to-light mapping. Ports 0x100–0x103, 8 bits each.
// -1 = unused bit (no light on that position).
static const int s_djOutMap[4][8] = {
    // Port 0x100: Lamps + Neons
    { LIGHT_RED_LAMP_L,  LIGHT_RED_LAMP_R,  LIGHT_BLUE_LAMP_L, LIGHT_BLUE_LAMP_R, LIGHT_NEONS,        -1, -1, -1 },
    // Port 0x101: Starts + Effectors
    { LIGHT_P1_START,    LIGHT_P2_START,    LIGHT_EFFECTOR_1,  LIGHT_EFFECTOR_2,  LIGHT_EFFECTOR_3,   LIGHT_EFFECTOR_4, -1, -1 },
    // Port 0x102: P1 Buttons + P1 Turntable
    { LIGHT_P1_1,        LIGHT_P1_2,        LIGHT_P1_3,        LIGHT_P1_4,        LIGHT_P1_5,         LIGHT_P1_TURNTABLE, -1, -1 },
    // Port 0x103: P2 Buttons + P2 Turntable
    { LIGHT_P2_1,        LIGHT_P2_2,        LIGHT_P2_3,        LIGHT_P2_4,        LIGHT_P2_5,         LIGHT_P2_TURNTABLE, -1, -1 },
};

static volatile float s_lightState[BindingStore::LIGHT_COUNT] = {};
static volatile bool  s_lightDirty = false;

void handleDJOut(uint16_t port, uint8_t value, const BindingStore& bs) {
    if (port < 0x100 || port > 0x103) return;

    const int* map = s_djOutMap[port - 0x100];
    for (int bit = 0; bit < 8; ++bit) {
        int idx = map[bit];
        if (idx < 0 || !bs.lights[idx].isSet()) continue;
        s_lightState[idx] = (value & (1 << bit)) ? 1.0f : 0.0f;
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

    for (;;) {
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
