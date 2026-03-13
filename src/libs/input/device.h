#pragma once

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
struct DeviceHIDInfo {
    PHIDP_PREPARSED_DATA preparsed  = nullptr;
    HIDP_CAPS            caps       = {};
    HANDLE               hidHandle  = INVALID_HANDLE_VALUE;

    // Input cap lists for WM_INPUT parsing.
    std::vector<HIDP_BUTTON_CAPS> buttonCapsList;
    std::vector<HIDP_VALUE_CAPS>  valueCapsList;  // includes hat switch caps

    // Live input state — updated under csInput by WM_INPUT handler.
    // buttonStates[i] matches buttonCapsNames[i].
    std::vector<bool>  buttonStates;
    // valueStates[i] is float. For regular axes: [0.0, 1.0] normalized.
    // For hat switches: -1.0f = neutral, 0.0-1.0 = direction scaled from LogicalMin..LogicalMax.
    std::vector<float> valueStates;
    // valueStatesRaw[i] is the raw LONG from HidP_GetUsageValue after sign-extension fix.
    std::vector<LONG>  valueStatesRaw;

    // Output cap lists for output report building.
    std::vector<HIDP_BUTTON_CAPS> buttonOutputCapsList;
    std::vector<HIDP_VALUE_CAPS>  valueOutputCapsList;
    // Specific HID usage for each valueOutputCapsList entry.
    // Range caps are expanded so each entry has exactly one usage here.
    std::vector<USAGE> valueOutputUsages;

    // Output state arrays — flat bool/float indexed same as buttonOutputCapsNames / valueOutputCapsNames.
    std::vector<bool>  buttonOutputStates;
    std::vector<float> valueOutputStates;
};

// One entry per connected HID device.
// path = full Windows device path from GetRawInputDeviceInfo(RIDI_DEVICENAME).
// This is the stable opaque key used for device lookup by the binding layer.
struct Device {
    std::string path;   // e.g. "\\?\HID#VID_0810&PID_E501#9&2fc2c90&0&0000#{...}"
    std::string name;   // product string or "VID_XXXX:PID_XXXX" fallback

    // Pre-built name arrays — index IS the buttonIdx / axisIdx used in bindings.
    // Hat switch caps appear in valueCapsNames (not promoted to buttons).
    std::vector<std::string> buttonCapsNames;
    // valueCapsNames[] includes hat switch caps (named "Hat Switch").
    std::vector<std::string> valueCapsNames;

    // Output cap name arrays — pre-built for light output.
    std::vector<std::string> buttonOutputCapsNames;
    std::vector<std::string> valueOutputCapsNames;

    // Per-device locks (inline, not heap-allocated).
    CRITICAL_SECTION csInput;   // protects DeviceHIDInfo buttonStates, valueStates
    CRITICAL_SECTION csOutput;  // protects DeviceHIDInfo output states, outputPending, outputEnabled

    // Device must opt-in to receive light writes.
    // Set true when hidHandle != INVALID_HANDLE_VALUE && OutputReportByteLength > 0.
    bool outputEnabled = false;

    // Signals output thread that state has changed.
    bool outputPending = false;

    // HID-specific data. Null if device has no usable HID caps.
    DeviceHIDInfo* hid = nullptr;

    // Internal: Raw Input kernel handle (from GetRawInputDeviceList, NOT CreateFile).
    HANDLE rawHandle = nullptr;

    bool isValid() const { return rawHandle != nullptr && hid != nullptr; }

    void destroy() {
        if (hid) {
            if (hid->preparsed) {
                LocalFree(hid->preparsed);
                hid->preparsed = nullptr;
            }
            if (hid->hidHandle != INVALID_HANDLE_VALUE) {
                CloseHandle(hid->hidHandle);
                hid->hidHandle = INVALID_HANDLE_VALUE;
            }
            delete hid;
            hid = nullptr;
        }
        DeleteCriticalSection(&csInput);
        DeleteCriticalSection(&csOutput);
    }
};

// Result returned by InputManager::pollCapture().
struct CaptureResult {
    std::string      path;        // full device path — same as Device::path
    int              buttonIdx;   // flat index into buttonCapsNames[]/buttonStates[] or valueStates[] for hats
    std::string      deviceName;  // same as Device::name
    ButtonAnalogType analogType = ButtonAnalogType::NONE;  // non-NONE for hat direction captures
};
