#include "dll_input.h"
#include "bindings.h"
#include "input_manager.h"
#include "game_defs.h"
#include <windows.h>
#include <atomic>

// DJ input port indices
static constexpr uint16_t PORT_DJ_CTRL       = 0x101;
static constexpr uint16_t PORT_DJ_P1_BUTTONS = 0x102;
static constexpr uint16_t PORT_DJ_P2_BUTTONS = 0x106;

// Dancer input port indices
static constexpr uint16_t PORT_DANCER_P1_FEET    = 0x300;
static constexpr uint16_t PORT_DANCER_P2_FEET    = 0x302;
static constexpr uint16_t PORT_DANCER_HAND_SENSOR = 0x306;

// Pre-computed port cache. VEH handler reads from here (single atomic read).
std::atomic<uint8_t>  s_djPortCache[7]     = { 0xFF, 0xFF, 0xFF, 0x80, 0x80, 0xFF, 0xFF };
std::atomic<uint16_t> s_dancerPortCache[4] = { 0xF000, 0xF000, 0x0000, 0x00FF };

// Unique HID device paths referenced by loaded bindings.
// Built once in initPortCache(); never changes during DLL lifetime.
static std::vector<std::string> s_boundDevicePaths;

static uint8_t computePort0x101(const BindingStore& bs, const BindingStore::SnapMap& snap) {
    uint8_t r = 0xFF;
    if (bs.isHeldSnapshot(bs.buttons[(int)DJButton::P1_START],   snap)) r &= ~0x01;
    if (bs.isHeldSnapshot(bs.buttons[(int)DJButton::P2_START],   snap)) r &= ~0x02;
    if (bs.isHeldSnapshot(bs.buttons[(int)DJButton::EFFECTOR_1], snap)) r &= ~0x04;
    if (bs.isHeldSnapshot(bs.buttons[(int)DJButton::EFFECTOR_2], snap)) r &= ~0x08;
    if (bs.isHeldSnapshot(bs.buttons[(int)DJButton::EFFECTOR_3], snap)) r &= ~0x10;
    if (bs.isHeldSnapshot(bs.buttons[(int)DJButton::EFFECTOR_4], snap)) r &= ~0x20;
    if (bs.isHeldSnapshot(bs.buttons[(int)DJButton::SERVICE],    snap)) r &= ~0x40;
    if (bs.isHeldSnapshot(bs.buttons[(int)DJButton::TEST],       snap)) r &= ~0x80;
    return r;
}

static uint8_t computePort0x102(const BindingStore& bs, const BindingStore::SnapMap& snap) {
    uint8_t r = 0xFF;
    if (bs.isHeldSnapshot(bs.buttons[(int)DJButton::P1_1],    snap)) r &= ~0x01;
    if (bs.isHeldSnapshot(bs.buttons[(int)DJButton::P1_2],    snap)) r &= ~0x02;
    if (bs.isHeldSnapshot(bs.buttons[(int)DJButton::P1_3],    snap)) r &= ~0x04;
    if (bs.isHeldSnapshot(bs.buttons[(int)DJButton::P1_4],    snap)) r &= ~0x08;
    if (bs.isHeldSnapshot(bs.buttons[(int)DJButton::P1_5],    snap)) r &= ~0x10;
    // bits 5-6 unused
    if (bs.isHeldSnapshot(bs.buttons[(int)DJButton::P1_PEDAL], snap)) r &= ~0x80;
    return r;
}

static uint8_t computePort0x106(const BindingStore& bs, const BindingStore::SnapMap& snap) {
    uint8_t r = 0xFF;
    if (bs.isHeldSnapshot(bs.buttons[(int)DJButton::P2_1],    snap)) r &= ~0x01;
    if (bs.isHeldSnapshot(bs.buttons[(int)DJButton::P2_2],    snap)) r &= ~0x02;
    if (bs.isHeldSnapshot(bs.buttons[(int)DJButton::P2_3],    snap)) r &= ~0x04;
    if (bs.isHeldSnapshot(bs.buttons[(int)DJButton::P2_4],    snap)) r &= ~0x08;
    if (bs.isHeldSnapshot(bs.buttons[(int)DJButton::P2_5],    snap)) r &= ~0x10;
    // bits 5-6 unused
    if (bs.isHeldSnapshot(bs.buttons[(int)DJButton::P2_PEDAL], snap)) r &= ~0x80;
    return r;
}

static uint16_t computePort0x300(const BindingStore& bs, const BindingStore::SnapMap& snap) {
    uint16_t r = 0x0FFF;
    if (bs.isHeldSnapshot(bs.dancerButtons[(int)DancerButton::P1_LEFT],   snap)) r &= ~0x00F;
    if (bs.isHeldSnapshot(bs.dancerButtons[(int)DancerButton::P1_CENTRE], snap)) r &= ~0x0F0;
    if (bs.isHeldSnapshot(bs.dancerButtons[(int)DancerButton::P1_RIGHT],  snap)) r &= ~0xF00;
    return static_cast<uint16_t>(~r);
}

static uint16_t computePort0x302(const BindingStore& bs, const BindingStore::SnapMap& snap) {
    uint16_t r = 0x0FFF;
    if (bs.isHeldSnapshot(bs.dancerButtons[(int)DancerButton::P2_LEFT],   snap)) r &= ~0x00F;
    if (bs.isHeldSnapshot(bs.dancerButtons[(int)DancerButton::P2_CENTRE], snap)) r &= ~0x0F0;
    if (bs.isHeldSnapshot(bs.dancerButtons[(int)DancerButton::P2_RIGHT],  snap)) r &= ~0xF00;
    return static_cast<uint16_t>(~r);
}

static uint16_t computePort0x306(const BindingStore& bs, const BindingStore::SnapMap& snap) {
    uint16_t r = 0xFFFF;
    if (bs.isHeldSnapshot(bs.dancerButtons[(int)DancerButton::P1_L_SENSOR_TOP], snap)) r &= ~(1 << 11);
    if (bs.isHeldSnapshot(bs.dancerButtons[(int)DancerButton::P1_L_SENSOR_BOT], snap)) r &= ~(1 << 12);
    if (bs.isHeldSnapshot(bs.dancerButtons[(int)DancerButton::P1_R_SENSOR_TOP], snap)) r &= ~(1 << 10);
    if (bs.isHeldSnapshot(bs.dancerButtons[(int)DancerButton::P1_R_SENSOR_BOT], snap)) r &= ~(1 << 13);
    if (bs.isHeldSnapshot(bs.dancerButtons[(int)DancerButton::P2_L_SENSOR_TOP], snap)) r &= ~(1 <<  9);
    if (bs.isHeldSnapshot(bs.dancerButtons[(int)DancerButton::P2_L_SENSOR_BOT], snap)) r &= ~(1 << 14);
    if (bs.isHeldSnapshot(bs.dancerButtons[(int)DancerButton::P2_R_SENSOR_TOP], snap)) r &= ~(1 <<  8);
    if (bs.isHeldSnapshot(bs.dancerButtons[(int)DancerButton::P2_R_SENSOR_BOT], snap)) r &= ~(1 << 15);
    if (bs.isHeldSnapshot(bs.dancerButtons[(int)DancerButton::TEST],            snap)) r &= 0xFF00 | (1 << 5);
    if (bs.isHeldSnapshot(bs.dancerButtons[(int)DancerButton::SERVICE],         snap)) r &= 0xFF00 | (1 << 4);
    return r ^ 0xFF00;
}

void updatePortCache(const BindingStore& bs) {
    BindingStore::SnapMap snap;
    for (const auto& path : s_boundDevicePaths) {
        DeviceSnapshot ds;
        if (bs.mgr->snapshotDevice(path, ds))
            snap[path] = std::move(ds);
    }
    // DJ button ports
    s_djPortCache[1].store(computePort0x101(bs, snap));
    s_djPortCache[2].store(computePort0x102(bs, snap));
    s_djPortCache[6].store(computePort0x106(bs, snap));

    // DJ analog ports (turntables)
    s_djPortCache[3].store(bs.getPositionSnapshot(bs.analogs[(int)Analog::P1_TURNTABLE],
                                                   bs.mgr->getVttPosition((int)Analog::P1_TURNTABLE), snap));
    s_djPortCache[4].store(bs.getPositionSnapshot(bs.analogs[(int)Analog::P2_TURNTABLE],
                                                   bs.mgr->getVttPosition((int)Analog::P2_TURNTABLE), snap));

    // Dancer ports
    s_dancerPortCache[0].store(computePort0x300(bs, snap));
    s_dancerPortCache[1].store(computePort0x302(bs, snap));
    s_dancerPortCache[3].store(computePort0x306(bs, snap));
}

static void addUnique(const std::string& p) {
    if (p.empty()) return;
    for (const std::string& existing : s_boundDevicePaths)
        if (existing == p) return;
    s_boundDevicePaths.push_back(p);
}

void initPortCache(const BindingStore& bs) {
    // Bindings never change during DLL lifetime, so this runs exactly once.
    for (auto& b : bs.buttons)
        if (b.isSet() && !b.isKeyboard()) addUnique(b.device_path);
    for (auto& b : bs.dancerButtons)
        if (b.isSet() && !b.isKeyboard()) addUnique(b.device_path);
    for (auto& ab : bs.analogs) {
        if (ab.isSet())                                              addUnique(ab.device_path);
        if (ab.vtt_plus.isSet()  && !ab.vtt_plus.isKeyboard())  addUnique(ab.vtt_plus.device_path);
        if (ab.vtt_minus.isSet() && !ab.vtt_minus.isKeyboard()) addUnique(ab.vtt_minus.device_path);
    }

    // Populate cache immediately so the game doesn't read stale 0xFF values.
    updatePortCache(bs);

    bs.mgr->setInputCallback([](void* ud) {
        updatePortCache(*static_cast<const BindingStore*>(ud));
    }, const_cast<BindingStore*>(&bs));
}
