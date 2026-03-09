#include "dll_input.h"
#include "bindings.h"
#include "input_manager.h"
#include "strings.h"
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

// --- DJ port computation ----------------------------------------------------

static uint8_t computePort0x101(const BindingStore& bs) {
    uint8_t r = 0xFF;
    if (bs.isHeld(bs.buttons[DJ_P1_START]))   r &= ~0x01;
    if (bs.isHeld(bs.buttons[DJ_P2_START]))   r &= ~0x02;
    if (bs.isHeld(bs.buttons[DJ_EFFECTOR_1])) r &= ~0x04;
    if (bs.isHeld(bs.buttons[DJ_EFFECTOR_2])) r &= ~0x08;
    if (bs.isHeld(bs.buttons[DJ_EFFECTOR_3])) r &= ~0x10;
    if (bs.isHeld(bs.buttons[DJ_EFFECTOR_4])) r &= ~0x20;
    if (bs.isHeld(bs.buttons[DJ_SERVICE]))    r &= ~0x40;
    if (bs.isHeld(bs.buttons[DJ_TEST]))       r &= ~0x80;
    return r;
}

static uint8_t computePort0x102(const BindingStore& bs) {
    uint8_t r = 0xFF;
    if (bs.isHeld(bs.buttons[DJ_P1_1]))    r &= ~0x01;
    if (bs.isHeld(bs.buttons[DJ_P1_2]))    r &= ~0x02;
    if (bs.isHeld(bs.buttons[DJ_P1_3]))    r &= ~0x04;
    if (bs.isHeld(bs.buttons[DJ_P1_4]))    r &= ~0x08;
    if (bs.isHeld(bs.buttons[DJ_P1_5]))    r &= ~0x10;
    // bits 5-6 unused
    if (bs.isHeld(bs.buttons[DJ_P1_PEDAL])) r &= ~0x80;
    return r;
}

static uint8_t computePort0x106(const BindingStore& bs) {
    uint8_t r = 0xFF;
    if (bs.isHeld(bs.buttons[DJ_P2_1]))    r &= ~0x01;
    if (bs.isHeld(bs.buttons[DJ_P2_2]))    r &= ~0x02;
    if (bs.isHeld(bs.buttons[DJ_P2_3]))    r &= ~0x04;
    if (bs.isHeld(bs.buttons[DJ_P2_4]))    r &= ~0x08;
    if (bs.isHeld(bs.buttons[DJ_P2_5]))    r &= ~0x10;
    // bits 5-6 unused
    if (bs.isHeld(bs.buttons[DJ_P2_PEDAL])) r &= ~0x80;
    return r;
}

// --- Dancer port computation ------------------------------------------------

// Foot ports: each panel press clears a 4-bit nibble, result is inverted.
static uint16_t computePort0x300(const BindingStore& bs) {
    uint16_t r = 0x0FFF;
    if (bs.isHeld(bs.dancerButtons[DANCER_P1_LEFT]))   r &= ~0x00F;
    if (bs.isHeld(bs.dancerButtons[DANCER_P1_CENTRE])) r &= ~0x0F0;
    if (bs.isHeld(bs.dancerButtons[DANCER_P1_RIGHT]))  r &= ~0xF00;
    return static_cast<uint16_t>(~r);
}

static uint16_t computePort0x302(const BindingStore& bs) {
    uint16_t r = 0x0FFF;
    if (bs.isHeld(bs.dancerButtons[DANCER_P2_LEFT]))   r &= ~0x00F;
    if (bs.isHeld(bs.dancerButtons[DANCER_P2_CENTRE])) r &= ~0x0F0;
    if (bs.isHeld(bs.dancerButtons[DANCER_P2_RIGHT]))  r &= ~0xF00;
    return static_cast<uint16_t>(~r);
}

// Hand sensor port (0x306): active-high, test/service in low byte.
static uint16_t computePort0x306(const BindingStore& bs) {
    uint16_t r = 0xFFFF;
    if (bs.isHeld(bs.dancerButtons[DANCER_P1_L_SENSOR_TOP])) r &= ~(1 << 11);
    if (bs.isHeld(bs.dancerButtons[DANCER_P1_L_SENSOR_BOT])) r &= ~(1 << 12);
    if (bs.isHeld(bs.dancerButtons[DANCER_P1_R_SENSOR_TOP])) r &= ~(1 << 10);
    if (bs.isHeld(bs.dancerButtons[DANCER_P1_R_SENSOR_BOT])) r &= ~(1 << 13);
    if (bs.isHeld(bs.dancerButtons[DANCER_P2_L_SENSOR_TOP])) r &= ~(1 <<  9);
    if (bs.isHeld(bs.dancerButtons[DANCER_P2_L_SENSOR_BOT])) r &= ~(1 << 14);
    if (bs.isHeld(bs.dancerButtons[DANCER_P2_R_SENSOR_TOP])) r &= ~(1 <<  8);
    if (bs.isHeld(bs.dancerButtons[DANCER_P2_R_SENSOR_BOT])) r &= ~(1 << 15);
    if (bs.isHeld(bs.dancerButtons[DANCER_TEST]))             r &= 0xFF00 | (1 << 5);
    if (bs.isHeld(bs.dancerButtons[DANCER_SERVICE]))          r &= 0xFF00 | (1 << 4);
    return r ^ 0xFF00;
}

// --- Polling thread ---------------------------------------------------------

static DWORD WINAPI inputPollingThread(void* arg) {
    const BindingStore& bs = *static_cast<const BindingStore*>(arg);

    while (true) {
        Sleep(1);

        // DJ button ports
        s_djPortCache[1].store(computePort0x101(bs));
        s_djPortCache[2].store(computePort0x102(bs));
        s_djPortCache[6].store(computePort0x106(bs));

        // DJ analog ports (turntables)
        s_djPortCache[3].store(bs.getPosition(bs.analogs[ANALOG_P1_TURNTABLE], bs.mgr->getVttPosition(ANALOG_P1_TURNTABLE)));
        s_djPortCache[4].store(bs.getPosition(bs.analogs[ANALOG_P2_TURNTABLE], bs.mgr->getVttPosition(ANALOG_P2_TURNTABLE)));

        // Dancer ports
        s_dancerPortCache[0].store(computePort0x300(bs));
        s_dancerPortCache[1].store(computePort0x302(bs));
        s_dancerPortCache[3].store(computePort0x306(bs));
    }
    return 0;
}

void startInputPollingThread(const BindingStore& bs) {
    CreateThread(nullptr, 0, inputPollingThread, const_cast<BindingStore*>(&bs), 0, nullptr);
}
