#pragma once

#include "device.h"
#include <vector>
#include <optional>
#include <cstdint>
#include <string>

// Forward declaration — defined only in input_manager.cpp
struct InputManagerImpl;

// Immutable snapshot of a device's button and axis states.
// Copied from live state in a single cs_input acquisition.
struct DeviceSnapshot {
    std::vector<bool>  buttons;  // copy of button_states
    std::vector<float> values;   // copy of value_states (includes hat axes)
};

class InputManager {
public:
    InputManager();
    ~InputManager();

    // Non-copyable
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    // Return list of currently enumerated HID devices.
    // Called from UI thread. Returns a snapshot (devices_reload() called once at init).
    std::vector<Device> getDevices() const;

    // Query live button state. Returns false if device not found or idx out of range.
    bool getButtonState(const std::string& path, int button_idx) const;

    // Query normalized axis value [0.0, 1.0]. Returns 0.5 if not found.
    float getAxisValue(const std::string& path, int axis_idx) const;

    // Configure VTT (virtual turntable) for a port. port=0 (P1), port=1 (P2).
    // plus_vk=0/minus_vk=0 disables VTT for that port.
    void setVttKeys(int port, int plus_vk, int minus_vk, int step);

    // Return accumulated VTT position [0..255], center=128.
    uint8_t getVttPosition(int port) const;

    // Capture mode: queue edge-detected button presses for pollCapture().
    void startCapture();
    void stopCapture();
    std::optional<CaptureResult> pollCapture();

    // Set light output. output_idx is flat index: button_output_caps first, then value_output_caps.
    // value in [0.0, 1.0]. Button outputs: value > 0.5 = on. Value outputs: written as float intensity.
    // Automatically enables output for the device on first call.
    void setLight(const std::string& path, int output_idx, float value);

    // Disable output for a device — stops flush thread from sending reports.
    void disableOutput(const std::string& path);

    // Copy all button and axis states for one device in a single cs_input acquisition.
    // Returns false if the device is not found.
    bool snapshotDevice(const std::string& path, DeviceSnapshot& out) const;

    // Register a callback fired from the TIME_CRITICAL message pump thread
    // immediately after each WM_INPUT event (all locks released).
    // Used by the DLL to update the port cache without a polling thread.
    void setInputCallback(void(*fn)(void*), void* userdata);

private:
    InputManagerImpl* impl;
};
