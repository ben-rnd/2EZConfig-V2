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

namespace Input {

// DeviceDesc: lightweight descriptor for UI enumeration (Phase 2)
struct DeviceDesc {
    std::string path;          // HID device path (use as binding device_path)
    std::string manufacturer;  // from HidD_GetManufacturerString
    std::string product;       // from HidD_GetProductString
};

// Initialize the input subsystem: enumerate and open HID devices, start polling
// and VTT threads. Call once at startup (DLL inject or config EXE launch).
void init(SettingsManager& settings);

// Stop polling threads, close all HID handles, release preparsed data.
// Call from DLL_PROCESS_DETACH or config EXE shutdown.
void shutdown();

// Return list of currently attached HID devices for UI display (Phase 2).
// Safe to call from UI thread; does not interact with polling state.
std::vector<DeviceDesc> enumerateDevices();

// Return the active-low button state for the given game action name.
// Returns true (pressed) or false (not pressed / unbound).
// Reads from lock-free shared state — safe to call from VEH handler.
// action must match a string from ioButtons[] in strings.h.
bool getButtonState(const std::string& gameAction);

// Return the combined turntable position [0-255] for the given action name.
// Combined value = scaled_axis_value + vtt_delta (uint8 wraparound).
// Returns 128 (center) if no axis or VTT binding is present.
// Reads from lock-free shared state — safe to call from VEH handler.
// action must match a string from analogs[] in strings.h.
uint8_t getAnalogValue(const std::string& gameAction);

// Result type for listen/capture mode (Phase 2 Buttons tab)
struct ButtonPressResult {
    std::string device_path;
    uint16_t usage_page;
    uint16_t usage_id;
};

// Poll all open device handles once for any newly-pressed HID button.
// Returns the first button detected as newly-pressed (was not pressed on prior call).
// Returns empty optional if no new press detected.
// Call from UI thread during listen/capture mode — safe alongside polling thread
// because it uses the same shared device handles (FILE_SHARE_READ|WRITE).
// Internal state (previous button set) is static — call once per frame while listening.
std::optional<ButtonPressResult> pollNextButtonPress();

} // namespace Input
