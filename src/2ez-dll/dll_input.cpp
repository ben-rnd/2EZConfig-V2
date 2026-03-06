#include "dll_input.h"
#include "bindings.h"
#include "input_manager.h"
#include "strings.h"
#include <windows.h>

static const float HAT_SWITCH_INCREMENT = 1.0f / 7.0f;

// Pre-computed port cache. VEH handler reads from here (single volatile read).
volatile uint8_t  s_djPortCache[7]     = { 0xFF, 0xFF, 0xFF, 0x80, 0x80, 0xFF, 0xFF };
volatile uint16_t s_dancerPortCache[4] = { 0xF000, 0xF000, 0x0000, 0x00FF };

static bool isHatDirectionActive(float hat_val, ButtonAnalogType dir) {
    if (hat_val < 0.0f) return false;
    int dir_idx = (int)dir - (int)ButtonAnalogType::HS_UP;
    float target = dir_idx * HAT_SWITCH_INCREMENT;
    float diff = hat_val - target;
    if (diff > 0.5f) diff -= 1.0f;
    if (diff < -0.5f) diff += 1.0f;
    return (diff >= -HAT_SWITCH_INCREMENT * 0.5f - 0.001f &&
            diff <=  HAT_SWITCH_INCREMENT * 0.5f + 0.001f);
}

static bool isPressed(const ButtonBinding& b, InputManager& mgr) {
    if (!b.isSet()) return false;
    if (b.isKeyboard()) return (GetAsyncKeyState(b.vk_code) & 0x8000) != 0;
    if (b.analog_type != ButtonAnalogType::NONE) {
        float hat_val = mgr.getAxisValue(b.device_path, b.button_idx);
        return isHatDirectionActive(hat_val, b.analog_type);
    }
    return mgr.getButtonState(b.device_path, b.button_idx);
}

// --- DJ port computation ----------------------------------------------------

static uint8_t computePort0x101(const BindingStore& bs, InputManager& mgr) {
    uint8_t r = 0xFF;
    if (isPressed(bs.buttons[DJ_P1_START],   mgr)) r &= ~0x01;
    if (isPressed(bs.buttons[DJ_P2_START],   mgr)) r &= ~0x02;
    if (isPressed(bs.buttons[DJ_EFFECTOR_1], mgr)) r &= ~0x04;
    if (isPressed(bs.buttons[DJ_EFFECTOR_2], mgr)) r &= ~0x08;
    if (isPressed(bs.buttons[DJ_EFFECTOR_3], mgr)) r &= ~0x10;
    if (isPressed(bs.buttons[DJ_EFFECTOR_4], mgr)) r &= ~0x20;
    if (isPressed(bs.buttons[DJ_SERVICE],    mgr)) r &= ~0x40;
    if (isPressed(bs.buttons[DJ_TEST],       mgr)) r &= ~0x80;
    return r;
}

static uint8_t computePort0x102(const BindingStore& bs, InputManager& mgr) {
    uint8_t r = 0xFF;
    if (isPressed(bs.buttons[DJ_P1_1],    mgr)) r &= ~0x01;
    if (isPressed(bs.buttons[DJ_P1_2],    mgr)) r &= ~0x02;
    if (isPressed(bs.buttons[DJ_P1_3],    mgr)) r &= ~0x04;
    if (isPressed(bs.buttons[DJ_P1_4],    mgr)) r &= ~0x08;
    if (isPressed(bs.buttons[DJ_P1_5],    mgr)) r &= ~0x10;
    // bits 5-6 unused
    if (isPressed(bs.buttons[DJ_P1_PEDAL], mgr)) r &= ~0x80;
    return r;
}

static uint8_t computePort0x106(const BindingStore& bs, InputManager& mgr) {
    uint8_t r = 0xFF;
    if (isPressed(bs.buttons[DJ_P2_1],    mgr)) r &= ~0x01;
    if (isPressed(bs.buttons[DJ_P2_2],    mgr)) r &= ~0x02;
    if (isPressed(bs.buttons[DJ_P2_3],    mgr)) r &= ~0x04;
    if (isPressed(bs.buttons[DJ_P2_4],    mgr)) r &= ~0x08;
    if (isPressed(bs.buttons[DJ_P2_5],    mgr)) r &= ~0x10;
    // bits 5-6 unused
    if (isPressed(bs.buttons[DJ_P2_PEDAL], mgr)) r &= ~0x80;
    return r;
}

// --- Dancer port computation ------------------------------------------------

// Foot ports: each panel press clears a 4-bit nibble, result is inverted.
static uint16_t computePort0x300(const BindingStore& bs, InputManager& mgr) {
    uint16_t r = 0x0FFF;
    if (isPressed(bs.dancerButtons[DANCER_P1_LEFT],   mgr)) r &= ~0x00F;
    if (isPressed(bs.dancerButtons[DANCER_P1_CENTRE], mgr)) r &= ~0x0F0;
    if (isPressed(bs.dancerButtons[DANCER_P1_RIGHT],  mgr)) r &= ~0xF00;
    return static_cast<uint16_t>(~r);
}

static uint16_t computePort0x302(const BindingStore& bs, InputManager& mgr) {
    uint16_t r = 0x0FFF;
    if (isPressed(bs.dancerButtons[DANCER_P2_LEFT],   mgr)) r &= ~0x00F;
    if (isPressed(bs.dancerButtons[DANCER_P2_CENTRE], mgr)) r &= ~0x0F0;
    if (isPressed(bs.dancerButtons[DANCER_P2_RIGHT],  mgr)) r &= ~0xF00;
    return static_cast<uint16_t>(~r);
}

// Hand sensor port (0x306): active-high, test/service in low byte.
static uint16_t computePort0x306(const BindingStore& bs, InputManager& mgr) {
    uint16_t r = 0xFFFF;
    if (isPressed(bs.dancerButtons[DANCER_P1_L_SENSOR_TOP], mgr)) r &= ~(1 << 11);
    if (isPressed(bs.dancerButtons[DANCER_P1_L_SENSOR_BOT], mgr)) r &= ~(1 << 12);
    if (isPressed(bs.dancerButtons[DANCER_P1_R_SENSOR_TOP], mgr)) r &= ~(1 << 10);
    if (isPressed(bs.dancerButtons[DANCER_P1_R_SENSOR_BOT], mgr)) r &= ~(1 << 13);
    if (isPressed(bs.dancerButtons[DANCER_P2_L_SENSOR_TOP], mgr)) r &= ~(1 <<  9);
    if (isPressed(bs.dancerButtons[DANCER_P2_L_SENSOR_BOT], mgr)) r &= ~(1 << 14);
    if (isPressed(bs.dancerButtons[DANCER_P2_R_SENSOR_TOP], mgr)) r &= ~(1 <<  8);
    if (isPressed(bs.dancerButtons[DANCER_P2_R_SENSOR_BOT], mgr)) r &= ~(1 << 15);
    if (isPressed(bs.dancerButtons[DANCER_TEST],            mgr)) r &= 0xFF00 | (1 << 5);
    if (isPressed(bs.dancerButtons[DANCER_SERVICE],         mgr)) r &= 0xFF00 | (1 << 4);
    return r ^ 0xFF00;
}

// --- Polling thread ---------------------------------------------------------

struct InputPollArgs {
    const BindingStore* bs;
    InputManager* mgr;
};

static DWORD WINAPI inputPollingThread(void* arg) {
    auto* ctx = static_cast<InputPollArgs*>(arg);
    const BindingStore& bs = *ctx->bs;
    InputManager& mgr = *ctx->mgr;

    while (true) {
        Sleep(1);

        // DJ button ports
        s_djPortCache[1] = computePort0x101(bs, mgr);
        s_djPortCache[2] = computePort0x102(bs, mgr);
        s_djPortCache[6] = computePort0x106(bs, mgr);

        // DJ analog ports (turntables)
        s_djPortCache[3] = bs.analogs[ANALOG_P1_TURNTABLE].getPosition(mgr, mgr.getVttPosition(ANALOG_P1_TURNTABLE));
        s_djPortCache[4] = bs.analogs[ANALOG_P2_TURNTABLE].getPosition(mgr, mgr.getVttPosition(ANALOG_P2_TURNTABLE));

        // Dancer ports
        s_dancerPortCache[0] = computePort0x300(bs, mgr);
        s_dancerPortCache[1] = computePort0x302(bs, mgr);
        s_dancerPortCache[3] = computePort0x306(bs, mgr);
    }
    return 0;
}

void startInputPollingThread(const BindingStore& bs, InputManager& mgr) {
    auto* ctx = new InputPollArgs{ &bs, &mgr };
    CreateThread(nullptr, 0, inputPollingThread, ctx, 0, nullptr);
}
