#pragma once

#include "device.h"
#include <vector>
#include <cstdint>
#include <string>

struct InputManagerImpl;

struct MouseDeviceInfo {
    std::string path;
    std::string name;
};

// Copied from live state in a single csInput acquisition.
struct DeviceSnapshot {
    std::vector<bool>  buttons;  // copy of buttonStates
    std::vector<float> values;   // copy of valueStates (includes hat axes)
};

class InputManager {
public:
    InputManager();
    ~InputManager();

    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    std::vector<Device> getDevices() const;
    bool getButtonState(const std::string& path, int buttonIdx) const;
    float getAxisValue(const std::string& path, int axisIdx) const;
    void setVttKeys(int port, int plusVk, int minusVk, int step);
    uint8_t getVttPosition(int port) const;
    void startCapture();
    void stopCapture();
    bool pollCapture(CaptureResult& out);

    void setLight(const std::string& path, int outputIdx, float value);
    void disableOutput(const std::string& path);
    bool snapshotDevice(const std::string& path, DeviceSnapshot& out) const;
    void setInputCallback(void(*fn)(void*), void* userdata);

    std::vector<MouseDeviceInfo> getMouseDevices() const;
    void setMouseBinding(int port, const std::string& devicePath, int axis, int sensitivity);
    uint8_t getMousePosition(int port) const;

private:
    InputManagerImpl* impl;
};
