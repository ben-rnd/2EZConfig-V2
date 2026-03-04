#include "dll_output.h"
#include "bindings.h"
#include "input_manager.h"
#include <windows.h>

// ---- Buffered light state ----
// VEH handler writes to this buffer (fast array stores on game thread).
// Background thread flushes dirty values to InputManager::setLight().

static volatile float s_lightState[BindingStore::LIGHT_COUNT] = {};
static volatile bool  s_lightDirty = false;

// ---- Port-light mapping table ----
// Verified against reference 2EZ.cpp:
//   0x100: NA NA NA NEONS LAMPX LAMPX LAMPX LAMPX
//   0x101: NA NA E4 E3 E2 E1 P2 P1
//   0x102: NA NA TT1 B5 B4 B3 B2 B1
//   0x103: NA NA TT2 B10 B9 B8 B7 B6

struct PortLightEntry {
    uint8_t bit_mask;
    int     light_idx;  // index into BindingStore::lights[] (-1 = end sentinel)
};

// DJ OUT port 0x100: lamps + neons
static const PortLightEntry s_djPort0x100[] = {
    { 0x01, 19 },   // bit0 -> Red Lamp L
    { 0x02, 20 },   // bit1 -> Red Lamp R
    { 0x04, 21 },   // bit2 -> Blue Lamp L
    { 0x08, 22 },   // bit3 -> Blue Lamp R
    { 0x10, 18 },   // bit4 -> Neons
    { 0, -1 }
};

// DJ OUT port 0x101: effectors + starts
static const PortLightEntry s_djPort0x101[] = {
    { 0x01,  4 },   // bit0 -> P1 Start
    { 0x02,  5 },   // bit1 -> P2 Start
    { 0x04,  0 },   // bit2 -> Effector 1
    { 0x08,  1 },   // bit3 -> Effector 2
    { 0x10,  2 },   // bit4 -> Effector 3
    { 0x20,  3 },   // bit5 -> Effector 4
    { 0, -1 }
};

// DJ OUT port 0x102: P1 buttons + P1 turntable
static const PortLightEntry s_djPort0x102[] = {
    { 0x01,  7 },   // bit0 -> P1 1
    { 0x02,  8 },   // bit1 -> P1 2
    { 0x04,  9 },   // bit2 -> P1 3
    { 0x08, 10 },   // bit3 -> P1 4
    { 0x10, 11 },   // bit4 -> P1 5
    { 0x20,  6 },   // bit5 -> P1 Turntable
    { 0, -1 }
};

// DJ OUT port 0x103: P2 buttons + P2 turntable
static const PortLightEntry s_djPort0x103[] = {
    { 0x01, 13 },   // bit0 -> P2 1
    { 0x02, 14 },   // bit1 -> P2 2
    { 0x04, 15 },   // bit2 -> P2 3
    { 0x08, 16 },   // bit3 -> P2 4
    { 0x10, 17 },   // bit4 -> P2 5
    { 0x20, 12 },   // bit5 -> P2 Turntable
    { 0, -1 }
};

static const PortLightEntry* getDJLightMap(uint16_t port) {
    switch (port) {
        case 0x100: return s_djPort0x100;
        case 0x101: return s_djPort0x101;
        case 0x102: return s_djPort0x102;
        case 0x103: return s_djPort0x103;
        default:    return nullptr;
    }
}

// ---- handleDJOut ----
// Decodes OUT port value into the shadow buffer. No setLight calls here —
// the background flush thread handles forwarding to InputManager.

void handleDJOut(uint16_t port, uint8_t value, const BindingStore& bs) {
    const PortLightEntry* entries = getDJLightMap(port);
    if (!entries) return;

    for (; entries->light_idx >= 0; ++entries) {
        if (!bs.lights[entries->light_idx].isSet()) continue;
        s_lightState[entries->light_idx] = (value & entries->bit_mask) ? 1.0f : 0.0f;
    }
    s_lightDirty = true;
}

void handleDancerOut(uint16_t port, uint8_t value, const BindingStore& bs) {
    // TODO: Dancer OUT port-to-light mapping not yet researched.
    (void)port; (void)value; (void)bs;
}

// ---- Background flush thread ----
// Runs at ~1ms intervals. When dirty, forwards all bound light values
// to InputManager::setLight(). Keeps VEH handler fast (array writes only).

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
    // Leaked intentionally — lives for process lifetime.
    auto* ctx = new LightFlushArgs{ &bs, &mgr };
    CreateThread(nullptr, 0, lightFlushThread, ctx, 0, nullptr);
}
