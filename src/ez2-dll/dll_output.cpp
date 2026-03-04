#include "dll_output.h"
#include "bindings.h"
#include "input_manager.h"

// ---- Port-light mapping table ----
// Maps OUT port bit positions to BindingStore::lights[] indices.
// Inferred from EZ2DJ ISA lighting board layout.
// If any mapping proves incorrect, correct the static tables below only.

struct PortLightEntry {
    uint8_t bit_mask;   // bit to test in the OUT value
    int     light_idx;  // index into BindingStore::lights[] (-1 = end sentinel)
};

// DJ OUT port 0x100: effectors, starts, neons
static const PortLightEntry s_djPort0x100[] = {
    { 0x01,  4 },   // bit0 -> P1 Start      (lights[4])
    { 0x02,  5 },   // bit1 -> P2 Start      (lights[5])
    { 0x04,  0 },   // bit2 -> Effector 1    (lights[0])
    { 0x08,  1 },   // bit3 -> Effector 2    (lights[1])
    { 0x10,  2 },   // bit4 -> Effector 3    (lights[2])
    { 0x20,  3 },   // bit5 -> Effector 4    (lights[3])
    { 0x40, 18 },   // bit6 -> Neons         (lights[18])
    { 0, -1 }       // sentinel
};

// DJ OUT port 0x105: P1 buttons + P1 turntable
static const PortLightEntry s_djPort0x105[] = {
    { 0x01,  7 },   // bit0 -> P1 1          (lights[7])
    { 0x02,  8 },   // bit1 -> P1 2          (lights[8])
    { 0x04,  9 },   // bit2 -> P1 3          (lights[9])
    { 0x08, 10 },   // bit3 -> P1 4          (lights[10])
    { 0x10, 11 },   // bit4 -> P1 5          (lights[11])
    { 0x20,  6 },   // bit5 -> P1 Turntable  (lights[6])
    { 0, -1 }
};

// DJ OUT port 0x107: P2 buttons + P2 turntable
static const PortLightEntry s_djPort0x107[] = {
    { 0x01, 13 },   // bit0 -> P2 1          (lights[13])
    { 0x02, 14 },   // bit1 -> P2 2          (lights[14])
    { 0x04, 15 },   // bit2 -> P2 3          (lights[15])
    { 0x08, 16 },   // bit3 -> P2 4          (lights[16])
    { 0x10, 17 },   // bit4 -> P2 5          (lights[17])
    { 0x20, 12 },   // bit5 -> P2 Turntable  (lights[12])
    { 0, -1 }
};

// DJ OUT port 0x108: lamps
static const PortLightEntry s_djPort0x108[] = {
    { 0x01, 19 },   // bit0 -> Red Lamp L    (lights[19])
    { 0x02, 20 },   // bit1 -> Red Lamp R    (lights[20])
    { 0x04, 21 },   // bit2 -> Blue Lamp L   (lights[21])
    { 0x08, 22 },   // bit3 -> Blue Lamp R   (lights[22])
    { 0, -1 }
};

// Returns the port light map for a DJ OUT port, or nullptr for unrecognized ports.
static const PortLightEntry* getDJLightMap(uint16_t port) {
    switch (port) {
        case 0x100: return s_djPort0x100;
        case 0x105: return s_djPort0x105;
        case 0x107: return s_djPort0x107;
        case 0x108: return s_djPort0x108;
        default:    return nullptr;
    }
}

// ---- handleDJOut ----
// Decodes OUT port value using static bit-to-light-index tables.
// Only calls setLight for bound channels (efficiency: skip unbound).
// InputManager's flush thread handles rate limiting — every OUT triggers immediately.

void handleDJOut(uint16_t port, uint8_t value, const BindingStore& bs, InputManager& mgr) {
    const PortLightEntry* entries = getDJLightMap(port);
    if (!entries) return;

    for (; entries->light_idx >= 0; ++entries) {
        const LightBinding& lb = bs.lights[entries->light_idx];
        if (!lb.isSet()) continue;  // skip unbound — per locked decision
        bool on = (value & entries->bit_mask) != 0;
        mgr.setLight(lb.device_path, lb.output_idx, on ? 1.0f : 0.0f);
    }
}

// ---- handleDancerOut ----
// Dancer OUT port-to-light mapping is not yet researched/documented.
// Implemented as a no-op stub. When ez2Dancer light port mapping is confirmed,
// add PortLightEntry tables here following the same pattern as DJ above.

void handleDancerOut(uint16_t port, uint8_t value, const BindingStore& bs, InputManager& mgr) {
    // TODO: Dancer OUT port-to-light mapping not yet researched.
    // When ez2DancerLights[] mapping is confirmed, add tables here
    // following the same PortLightEntry pattern as DJ.
    (void)port; (void)value; (void)bs; (void)mgr;
}
