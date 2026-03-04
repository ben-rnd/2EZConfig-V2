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

// One entry per connected HID device.
// path = full Windows device path from GetRawInputDeviceInfo(RIDI_DEVICENAME).
// This is the stable opaque key used for device lookup by the binding layer.
struct Device {
    std::string path;   // e.g. "\\?\HID#VID_0810&PID_E501#9&2fc2c90&0&0000#{...}"
    std::string name;   // product string or "VID_XXXX:PID_XXXX" fallback

    // Pre-built name arrays — index IS the button_idx / axis_idx used in bindings.
    // button_caps_names[] includes DPad directions from hat switch caps (8 per hat).
    // value_caps_names[] includes only non-hat value caps (hat is promoted to buttons).
    std::vector<std::string> button_caps_names;
    std::vector<std::string> value_caps_names;

    // Output cap name arrays — pre-built for light output.
    std::vector<std::string> button_output_caps_names;
    std::vector<std::string> value_output_caps_names;

    // Output cap struct lists — parallel to button_output_caps_names / value_output_caps_names.
    // Required for HidP_SetButtons / HidP_SetUsageValue in the output flush thread.
    std::vector<HIDP_BUTTON_CAPS> button_output_caps_list;
    std::vector<HIDP_VALUE_CAPS>  value_output_caps_list;
    // Specific HID usage for each value_output_caps_list entry.
    // Range caps are expanded so each entry has exactly one usage here.
    std::vector<USAGE> value_output_usages;

    // Output state arrays — flat bool/float indexed same as button_output_caps_names / value_output_caps_names.
    std::vector<bool>  button_output_states;
    std::vector<float> value_output_states;

    // Persistent GENERIC_READ|GENERIC_WRITE handle for HidD_SetOutputReport.
    // INVALID_HANDLE_VALUE if device denied write access (game controllers) — output silently skipped.
    HANDLE hid_handle = INVALID_HANDLE_VALUE;

    // Signals output flush thread that state has changed.
    bool output_pending = false;

    // Live state — updated under lock by WM_INPUT handler.
    // button_states[i] matches button_caps_names[i] exactly.
    std::vector<bool>  button_states;
    // value_states[i] is normalized float [0.0, 1.0]. 0.5 = center.
    // Relative axes accumulate into value_states; absolute axes normalize directly.
    std::vector<float> value_states;
    // value_states_raw[i] is the raw LONG from HidP_GetUsageValue after sign-extension fix.
    std::vector<LONG>  value_states_raw;

    // Internal: Raw Input kernel handle (from GetRawInputDeviceList, NOT CreateFile).
    HANDLE raw_handle = nullptr;

    // Internal: Preparsed HID data (from RIDI_PREPARSEDDATA, freed with LocalFree).
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    HIDP_CAPS caps = {};

    // Internal: cap lists for WM_INPUT parsing.
    std::vector<HIDP_BUTTON_CAPS> button_caps_list;  // excludes hat caps
    std::vector<HIDP_VALUE_CAPS>  value_caps_list;   // excludes hat caps (hat->buttons)

    // hat_cap_offsets[i] = starting button_states[] index for hat cap i.
    // Length == number of hat caps found.
    std::vector<int> hat_cap_offsets;

    // Internal: hat HIDP_VALUE_CAPS entries (used in WM_INPUT handler).
    std::vector<HIDP_VALUE_CAPS> hat_caps_list;

    bool isValid() const { return raw_handle != nullptr && preparsed != nullptr; }

    void destroy() {
        if (preparsed) { LocalFree(preparsed); preparsed = nullptr; }
        if (hid_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(hid_handle);
            hid_handle = INVALID_HANDLE_VALUE;
        }
    }
};

// Result returned by InputManager::pollCapture().
struct CaptureResult {
    std::string path;        // full device path — same as Device::path
    int         button_idx;  // flat index into button_caps_names[] and button_states[]
    std::string device_name; // same as Device::name
};
