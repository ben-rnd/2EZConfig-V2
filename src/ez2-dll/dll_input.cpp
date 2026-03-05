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
// BindingStore::buttons[] indices match ioButtons[] in strings.h:
//  0=Test  1=Service  2=Eff1  3=Eff2  4=Eff3  5=Eff4
//  6=P1Start  7=P2Start
//  8=P1-1  9=P1-2  10=P1-3  11=P1-4  12=P1-5  13=P1Pedal
// 14=P2-1 15=P2-2  16=P2-3  17=P2-4  18=P2-5  19=P2Pedal

static uint8_t computePort0x101(const BindingStore& bs, InputManager& mgr) {
    uint8_t r = 0xFF;
    if (isPressed(bs.buttons[6],  mgr)) r &= ~0x01;  // P1 Start
    if (isPressed(bs.buttons[7],  mgr)) r &= ~0x02;  // P2 Start
    if (isPressed(bs.buttons[2],  mgr)) r &= ~0x04;  // Effector 1
    if (isPressed(bs.buttons[3],  mgr)) r &= ~0x08;  // Effector 2
    if (isPressed(bs.buttons[4],  mgr)) r &= ~0x10;  // Effector 3
    if (isPressed(bs.buttons[5],  mgr)) r &= ~0x20;  // Effector 4
    if (isPressed(bs.buttons[1],  mgr)) r &= ~0x40;  // Service
    if (isPressed(bs.buttons[0],  mgr)) r &= ~0x80;  // Test
    return r;
}

static uint8_t computePort0x102(const BindingStore& bs, InputManager& mgr) {
    uint8_t r = 0xFF;
    if (isPressed(bs.buttons[8],  mgr)) r &= ~0x01;  // P1 1
    if (isPressed(bs.buttons[9],  mgr)) r &= ~0x02;  // P1 2
    if (isPressed(bs.buttons[10], mgr)) r &= ~0x04;  // P1 3
    if (isPressed(bs.buttons[11], mgr)) r &= ~0x08;  // P1 4
    if (isPressed(bs.buttons[12], mgr)) r &= ~0x10;  // P1 5
    // bits 5-6 unused
    if (isPressed(bs.buttons[13], mgr)) r &= ~0x80;  // P1 Pedal
    return r;
}

static uint8_t computePort0x106(const BindingStore& bs, InputManager& mgr) {
    uint8_t r = 0xFF;
    if (isPressed(bs.buttons[14], mgr)) r &= ~0x01;  // P2 1
    if (isPressed(bs.buttons[15], mgr)) r &= ~0x02;  // P2 2
    if (isPressed(bs.buttons[16], mgr)) r &= ~0x04;  // P2 3
    if (isPressed(bs.buttons[17], mgr)) r &= ~0x08;  // P2 4
    if (isPressed(bs.buttons[18], mgr)) r &= ~0x10;  // P2 5
    // bits 5-6 unused
    if (isPressed(bs.buttons[19], mgr)) r &= ~0x80;  // P2 Pedal
    return r;
}

// --- Dancer port computation ------------------------------------------------
// BindingStore::dancerButtons[] indices match ez2DancerIOButtons[] in strings.h:
//  0=Test  1=Service
//  2=P1Left  3=P1Centre  4=P1Right
//  5=P2Left  6=P2Centre  7=P2Right
//  8=P1LSensorTop   9=P1LSensorBot
// 10=P1RSensorTop  11=P1RSensorBot
// 12=P2LSensorTop  13=P2LSensorBot
// 14=P2RSensorTop  15=P2RSensorBot

// Foot ports: each panel press clears a 4-bit nibble, result is inverted.
static uint16_t computePort0x300(const BindingStore& bs, InputManager& mgr) {
    uint16_t r = 0x0FFF;
    if (isPressed(bs.dancerButtons[2], mgr)) r &= ~0x00F;  // P1 Left
    if (isPressed(bs.dancerButtons[3], mgr)) r &= ~0x0F0;  // P1 Centre
    if (isPressed(bs.dancerButtons[4], mgr)) r &= ~0xF00;  // P1 Right
    return static_cast<uint16_t>(~r);
}

static uint16_t computePort0x302(const BindingStore& bs, InputManager& mgr) {
    uint16_t r = 0x0FFF;
    if (isPressed(bs.dancerButtons[5], mgr)) r &= ~0x00F;  // P2 Left
    if (isPressed(bs.dancerButtons[6], mgr)) r &= ~0x0F0;  // P2 Centre
    if (isPressed(bs.dancerButtons[7], mgr)) r &= ~0xF00;  // P2 Right
    return static_cast<uint16_t>(~r);
}

// Hand sensor port (0x306): active-high, test/service in low byte.
static uint16_t computePort0x306(const BindingStore& bs, InputManager& mgr) {
    uint16_t r = 0xFFFF;
    if (isPressed(bs.dancerButtons[8],  mgr)) r &= ~(1 << 11);  // P1 L Sensor Top
    if (isPressed(bs.dancerButtons[9],  mgr)) r &= ~(1 << 12);  // P1 L Sensor Bot
    if (isPressed(bs.dancerButtons[10], mgr)) r &= ~(1 << 10);  // P1 R Sensor Top
    if (isPressed(bs.dancerButtons[11], mgr)) r &= ~(1 << 13);  // P1 R Sensor Bot
    if (isPressed(bs.dancerButtons[12], mgr)) r &= ~(1 <<  9);  // P2 L Sensor Top
    if (isPressed(bs.dancerButtons[13], mgr)) r &= ~(1 << 14);  // P2 L Sensor Bot
    if (isPressed(bs.dancerButtons[14], mgr)) r &= ~(1 <<  8);  // P2 R Sensor Top
    if (isPressed(bs.dancerButtons[15], mgr)) r &= ~(1 << 15);  // P2 R Sensor Bot
    if (isPressed(bs.dancerButtons[0],  mgr)) r &= 0xFF00 | (1 << 5);  // Test
    if (isPressed(bs.dancerButtons[1],  mgr)) r &= 0xFF00 | (1 << 4);  // Service
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
        s_djPortCache[3] = bs.analogs[0].getPosition(mgr, mgr.getVttPosition(0));
        s_djPortCache[4] = bs.analogs[1].getPosition(mgr, mgr.getVttPosition(1));

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
