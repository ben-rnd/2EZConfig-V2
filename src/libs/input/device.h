#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}
#include <string>
#include <vector>
#include <cstdint>

// Analog type for hat-as-button bindings.
// Regular digital buttons use NONE. Hat switch directions use HS_* values.
// Matches spice2x ButtonAnalogType.
enum class ButtonAnalogType {
    NONE = 0,
    HS_UP,
    HS_UP_RIGHT,
    HS_RIGHT,
    HS_DOWN_RIGHT,
    HS_DOWN,
    HS_DOWN_LEFT,
    HS_LEFT,
    HS_UP_LEFT,
};

// HID-specific data for a device. Heap-allocated, owned by Device.
// Null if device has no usable HID caps (keyboard, mouse, vendor-specific).
// Matches spice2x DeviceHIDInfo pattern.
struct DeviceHIDInfo {
    PHIDP_PREPARSED_DATA preparsed  = nullptr;
    HIDP_CAPS            caps       = {};
    HANDLE               hid_handle = INVALID_HANDLE_VALUE;

    // Input cap lists for WM_INPUT parsing.
    std::vector<HIDP_BUTTON_CAPS> button_caps_list;
    std::vector<HIDP_VALUE_CAPS>  value_caps_list;  // includes hat switch caps

    // Live input state — updated under cs_input by WM_INPUT handler.
    // button_states[i] matches button_caps_names[i].
    std::vector<bool>  button_states;
    // value_states[i] is float. For regular axes: [0.0, 1.0] normalized.
    // For hat switches: -1.0f = neutral, 0.0-1.0 = direction scaled from LogicalMin..LogicalMax.
    std::vector<float> value_states;
    // value_states_raw[i] is the raw LONG from HidP_GetUsageValue after sign-extension fix.
    std::vector<LONG>  value_states_raw;

    // Output cap lists for output report building.
    std::vector<HIDP_BUTTON_CAPS> button_output_caps_list;
    std::vector<HIDP_VALUE_CAPS>  value_output_caps_list;
    // Specific HID usage for each value_output_caps_list entry.
    // Range caps are expanded so each entry has exactly one usage here.
    std::vector<USAGE> value_output_usages;

    // Output state arrays — flat bool/float indexed same as button_output_caps_names / value_output_caps_names.
    std::vector<bool>  button_output_states;
    std::vector<float> value_output_states;
};

// One entry per connected HID device.
// path = full Windows device path from GetRawInputDeviceInfo(RIDI_DEVICENAME).
// This is the stable opaque key used for device lookup by the binding layer.
struct Device {
    std::string path;   // e.g. "\\?\HID#VID_0810&PID_E501#9&2fc2c90&0&0000#{...}"
    std::string name;   // product string or "VID_XXXX:PID_XXXX" fallback

    // Pre-built name arrays — index IS the button_idx / axis_idx used in bindings.
    // Hat switch caps appear in value_caps_names (not promoted to buttons).
    std::vector<std::string> button_caps_names;
    // value_caps_names[] includes hat switch caps (named "Hat Switch").
    std::vector<std::string> value_caps_names;

    // Output cap name arrays — pre-built for light output.
    std::vector<std::string> button_output_caps_names;
    std::vector<std::string> value_output_caps_names;

    // Per-device locks (inline, not heap-allocated).
    CRITICAL_SECTION cs_input;   // protects DeviceHIDInfo button_states, value_states
    CRITICAL_SECTION cs_output;  // protects DeviceHIDInfo output_states, output_pending, output_enabled

    // Device must opt-in to receive light writes.
    // Set true when hid_handle != INVALID_HANDLE_VALUE && OutputReportByteLength > 0.
    bool output_enabled = false;

    // Signals output thread that state has changed.
    bool output_pending = false;

    // HID-specific data. Null if device has no usable HID caps.
    DeviceHIDInfo* hid = nullptr;

    // Internal: Raw Input kernel handle (from GetRawInputDeviceList, NOT CreateFile).
    HANDLE raw_handle = nullptr;

    bool isValid() const { return raw_handle != nullptr && hid != nullptr; }

    void destroy() {
        if (hid) {
            if (hid->preparsed) { LocalFree(hid->preparsed); hid->preparsed = nullptr; }
            if (hid->hid_handle != INVALID_HANDLE_VALUE) {
                CloseHandle(hid->hid_handle);
                hid->hid_handle = INVALID_HANDLE_VALUE;
            }
            delete hid;
            hid = nullptr;
        }
        DeleteCriticalSection(&cs_input);
        DeleteCriticalSection(&cs_output);
    }
};

// Result returned by InputManager::pollCapture().
struct CaptureResult {
    std::string     path;         // full device path — same as Device::path
    int             button_idx;   // flat index into button_caps_names[]/button_states[] or value_states[] for hats
    std::string     device_name;  // same as Device::name
    ButtonAnalogType analog_type = ButtonAnalogType::NONE;  // non-NONE for hat direction captures
};
