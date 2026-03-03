#pragma once
#include <cstdint>
#include <string>

// Stable HID device identifier — does not change across USB ports or reboots.
// Shared between config EXE and DLL.
struct HidDeviceId {
    uint16_t vendor_id  = 0;
    uint16_t product_id = 0;
    uint8_t  instance   = 0;  // 0 = first matching VID/PID, 1 = second, etc.
};

// USB HID controller button -> game action
struct ButtonBinding {
    HidDeviceId device;
    uint16_t    usage_page = 0;
    uint16_t    usage_id   = 0;
};

// Keyboard key -> game action
struct KeyboardBinding {
    uint32_t vk_code = 0;
};

// USB HID analog axis -> turntable
struct AnalogBinding {
    HidDeviceId device;
    uint16_t    usage_page  = 0;
    uint16_t    usage_id    = 0;
    bool        reverse     = false;
    float       sensitivity = 1.0f;
    uint8_t     dead_zone   = 0;
};

// Virtual turntable keys -> turntable position accumulator
struct VTTBinding {
    uint32_t plus_vk  = 0;
    uint32_t minus_vk = 0;
    uint8_t  step     = 3;
};
