#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <cstdint>
#include <vector>
#include <optional>

namespace Input {

// Runtime device descriptor for UI enumeration.
// id is an opaque stable string: "VID_XXXX&PID_XXXX&Instance_Y" (uppercase hex, 4 digits).
// button_labels and axis_labels are pre-built flat arrays; their index IS the button_idx/axis_idx.
struct DeviceDesc {
    std::string id;    // "VID_XXXX&PID_XXXX&Instance_Y" — opaque, stable across plug cycles
    std::string name;  // product string; falls back to "VID_XXXX:PID_XXXX" if unavailable
    std::vector<std::string> button_labels;  // flat list; index is the button_idx
    std::vector<std::string> axis_labels;    // flat list; index is the axis_idx
};

// Capture result: a newly-pressed HID button while capture mode is active.
struct CaptureResult {
    std::string device_id;   // opaque string key — same format as DeviceDesc::id
    int         button_idx;  // flat index into button_labels[] and button_states[]
    std::string device_name; // same as DeviceDesc::name
};

// Initialize: enumerate HID devices, create Raw Input window, start threads.
// No SettingsManager parameter — binding parsing is the binding layer's responsibility.
void init();

// Shutdown: destroy Raw Input window, stop threads, free device data.
void shutdown();

// Return list of currently attached HID devices with pre-built button and axis info.
std::vector<DeviceDesc> getDevices();

// Return pressed state for a specific device button. Safe to call from UI thread.
// Returns false if device not found or button_idx out of range.
bool getButtonState(const std::string& device_id, int button_idx);

// Return normalized axis value [0.0, 1.0] for a specific device axis. Returns 0.5 (center) if not found.
float getAxisValue(const std::string& device_id, int axis_idx);

// Configure VTT (virtual turntable) keys for a port. port=0 for P1, port=1 for P2.
// Set plus_vk=0, minus_vk=0 to disable VTT for that port.
void setVttKeys(int port, int plus_vk, int minus_vk, int step);

// Return the current VTT accumulated position for a port [0..255], center=128.
uint8_t getVttPosition(int port);

// Enable capture mode: edge-detected button presses are queued for pollCapture().
void startCapture();

// Disable capture mode and clear any pending capture result.
void stopCapture();

// Poll for a newly-pressed HID button. Returns the first press detected since last call.
// Call once per frame from UI thread while capture mode is active.
std::optional<CaptureResult> pollCapture();

} // namespace Input
