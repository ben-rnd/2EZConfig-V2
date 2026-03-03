#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}
#include "settings.h"
#include <string>
#include <cstdint>
#include <vector>
#include <optional>
#include <utility>

namespace Input {

// Runtime device descriptor for UI enumeration.
// Axis caps pre-built from preparsed data — no secondary CreateFile needed.
struct DeviceDesc {
    uint16_t vendor_id  = 0;
    uint16_t product_id = 0;
    uint8_t  instance   = 0;
    std::string manufacturer;
    std::string product;
    std::string path;
    std::vector<std::pair<uint16_t, uint16_t>> axis_usages;  // (usage_page, usage_id)
    std::vector<std::string>                   axis_labels;   // human-readable names
};

// Initialize: enumerate HID devices, create Raw Input window, parse bindings.
void init(SettingsManager& settings);

// Shutdown: destroy Raw Input window, stop threads.
void shutdown();

// Return list of currently attached HID devices with pre-built axis info.
std::vector<DeviceDesc> enumerateDevices();

// Return pressed state for a game action. Safe to call from UI thread.
bool getButtonState(const std::string& gameAction);

// Return combined turntable position [0-255] for a game action. Safe to call from UI thread.
uint8_t getAnalogValue(const std::string& gameAction);

// Stable capture result — VID/PID, not device path.
struct ButtonCaptureResult {
    uint16_t    vendor_id;
    uint16_t    product_id;
    uint8_t     instance;
    uint16_t    usage_page;
    uint16_t    usage_id;
    std::string device_name; // human-readable product string; empty if unavailable
};

// Poll for a newly-pressed HID button. Returns the first press detected since last call.
// Call once per frame from UI thread while in listen mode.
std::optional<ButtonCaptureResult> pollNextButtonPress();

// Enable/disable listen mode. While enabled, edge-detected button presses are captured
// for pollNextButtonPress() rather than being routed to normal action bindings.
void setListenMode(bool enabled);

} // namespace Input
