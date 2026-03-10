#pragma once

#include "device.h"
#include <vector>
#include <cstdint>
#include <string>

struct InputManagerImpl;

// Copied from live state in a single cs_input acquisition.
struct DeviceSnapshot {
    std::vector<bool>  buttons;  // copy of button_states
    std::vector<float> values;   // copy of value_states (includes hat axes)
};

class InputManager {
public:
    InputManager();
    ~InputManager();

    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    std::vector<Device> getDevices() const;
    bool getButtonState(const std::string& path, int button_idx) const;
    float getAxisValue(const std::string& path, int axis_idx) const;
    void setVttKeys(int port, int plus_vk, int minus_vk, int step);
    uint8_t getVttPosition(int port) const;
    void startCapture();
    void stopCapture();
    bool pollCapture(CaptureResult& out);
    
    void setLight(const std::string& path, int output_idx, float value);
    void disableOutput(const std::string& path);
    bool snapshotDevice(const std::string& path, DeviceSnapshot& out) const;
    void setInputCallback(void(*fn)(void*), void* userdata);

private:
    InputManagerImpl* impl;
};
