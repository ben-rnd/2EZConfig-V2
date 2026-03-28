#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include "input_manager.h"
#include "settings.h"
#include "game_defs.h"

// JSON key for analog port
static inline const char* analogPortKey(int port) {
    return port == 0 ? "p1_turntable" : "p2_turntable";
}

struct BindingBase {
    std::string devicePath;
    std::string deviceName;

protected:
    void clearBase() {
        devicePath.clear();
        deviceName.clear();
    }
};

struct ButtonBinding : BindingBase {
    int              buttonIdx  = -1;
    int              vkCode     = 0;
    ButtonAnalogType analogType = ButtonAnalogType::NONE;  // hat-as-button direction

    bool isSet()      const { return (!devicePath.empty() && buttonIdx >= 0) || vkCode != 0; }
    bool isKeyboard() const { return vkCode != 0; }

    void clear() {
        clearBase();
        buttonIdx  = -1;
        vkCode     = 0;
        analogType = ButtonAnalogType::NONE;
    }

    nlohmann::json   toJson()  const;
    static ButtonBinding fromJson(const nlohmann::json& j);

    static ButtonBinding fromCapture(const CaptureResult& r) {
        ButtonBinding b;
        b.devicePath  = r.path;
        b.buttonIdx   = r.buttonIdx;
        b.deviceName  = r.deviceName;
        b.analogType  = r.analogType;
        return b;
    }
};

struct AnalogBinding : BindingBase {
    int           axisIdx  = -1;
    bool          reverse  = false;
    ButtonBinding vttPlus;
    ButtonBinding vttMinus;
    int           vttStep  = 3;
    std::string   mousePath;
    std::string   mouseName;
    int           mouseAxis = -1;        // -1=none, 0=X, 1=Y
    int           mouseSensitivity = 5;  // 1-20

    bool isSet()    const { return (!devicePath.empty() && axisIdx >= 0) || hasMouse(); }
    bool hasVtt()   const { return vttPlus.isSet() || vttMinus.isSet(); }
    bool hasMouse() const { return mouseAxis >= 0 && !mousePath.empty(); }

    void clear() {
        clearBase();
        axisIdx = -1;
        reverse  = false;
        vttPlus.clear();
        vttMinus.clear();
        vttStep = 3;
        mousePath.clear();
        mouseName.clear();
        mouseAxis = -1;
        mouseSensitivity = 5;
    }

    nlohmann::json   toJson()  const;
    static AnalogBinding fromJson(const nlohmann::json& j);
};

struct LightBinding : BindingBase {
    int outputIdx = -1;   // flat index: button_output_caps first, value_output_caps after

    bool isSet() const { return !devicePath.empty() && outputIdx >= 0; }

    void clear() {
        clearBase();
        outputIdx = -1;
    }

    nlohmann::json toJson() const;
    static LightBinding fromJson(const nlohmann::json& j);
};

struct BindingStore {
    static constexpr int BUTTON_COUNT = static_cast<int>(DJButton::COUNT);
    static constexpr int DANCER_COUNT = static_cast<int>(DancerButton::COUNT);
    static constexpr int ANALOG_COUNT = static_cast<int>(Analog::COUNT);
    static constexpr int LIGHT_COUNT  = static_cast<int>(Light::COUNT);
    static constexpr int DANCER_LIGHT_COUNT = static_cast<int>(DancerLight::COUNT);
    static constexpr int SABIN_BUTTON_COUNT = static_cast<int>(SabinButton::COUNT);
    static constexpr int SABIN_LIGHT_COUNT  = static_cast<int>(SabinLight::COUNT);

    ButtonBinding buttons[BUTTON_COUNT];
    ButtonBinding dancerButtons[DANCER_COUNT];
    ButtonBinding sabinButtons[SABIN_BUTTON_COUNT];
    AnalogBinding analogs[ANALOG_COUNT];
    LightBinding  lights[LIGHT_COUNT];
    LightBinding  dancerLights[DANCER_LIGHT_COUNT];
    LightBinding  sabinLights[SABIN_LIGHT_COUNT];

    InputManager* mgr = nullptr;

    volatile LONG vttPos[ANALOG_COUNT] = {TT_CENTER_INTERNAL, TT_CENTER_INTERNAL};
    volatile bool vttRunning = false;
    HANDLE vttThread = nullptr;
    void startVttThread();
    void stopVttThread();
    uint8_t getVttPosition(int port) const;

    void load(SettingsManager& settings, InputManager& mgr);
    void save(SettingsManager& settings) const;

    bool isHeld(const ButtonBinding& b) const;

    using DeviceSnapshotMap = std::unordered_map<std::string, DeviceSnapshot>;
    bool isHeldSnapshot(const ButtonBinding& b, const DeviceSnapshotMap& deviceSnapshots) const;
    uint8_t getPositionSnapshot(const AnalogBinding& a, uint8_t vttPos, uint8_t mousePos, const DeviceSnapshotMap& deviceSnapshots) const;

    std::string getDisplayString(const ButtonBinding& b) const;
    std::string getDisplayString(const AnalogBinding& a) const;
    std::string getDisplayString(const LightBinding& l) const;

    uint8_t getAnalogPosition(const AnalogBinding& a, uint8_t vttPos, uint8_t mousePos) const;
};
