#pragma once
#include <cstdint>
#include <string>

// Binding type tag — stored as "type" field in JSON
enum class BindingType : uint8_t {
    None = 0,
    HidButton,
    Keyboard,
    HidAxis,
    VirtualTT,
    MouseWheel,
};

// USB HID controller button -> game action
struct ButtonBinding {
    std::string device_path;  // verbatim path from GetRawInputDeviceInfo(RIDI_DEVICENAME)
    uint16_t usage_page;
    uint16_t usage_id;
};

// Keyboard key -> game action
struct KeyboardBinding {
    uint32_t vk_code;  // Windows VK_ constant
};

// USB HID analog axis -> turntable (INP-03, INP-06)
struct AnalogBinding {
    std::string device_path;
    uint16_t usage_page;
    uint16_t usage_id;
    bool     reverse;         // invert 0-255 output
    float    sensitivity;     // multiplier applied after scaling (default 1.0)
    uint8_t  dead_zone;       // 0-127; values within dead_zone of 128 snap to 128
};

// Virtual turntable keys -> turntable position accumulator (INP-05)
struct VTTBinding {
    uint32_t plus_vk;   // VK code: increment while held
    uint32_t minus_vk;  // VK code: decrement while held
    uint8_t  step;      // position units per 5ms tick (default 3)
};

// Mouse wheel -> turntable position accumulator (INP-04)
struct MouseWheelBinding {
    std::string device_path;  // HID path of the mouse device to open (from binding JSON)
    uint8_t step;             // position units per wheel notch (default 3)
};
