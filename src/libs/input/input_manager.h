#pragma once

#include "device.h"
#include <vector>
#include <optional>
#include <cstdint>
#include <string>

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

private:
    // Forward declare internals — defined in input_manager.cpp
    struct Impl;
    Impl* impl;
};
