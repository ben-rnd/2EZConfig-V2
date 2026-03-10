#pragma once

#include <windows.h>
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
    std::string device_path;
    std::string device_name;

protected:
    void clearBase() {
        device_path.clear();
        device_name.clear();
    }
};

struct ButtonBinding : BindingBase {
    int              button_idx = -1;
    int              vk_code    = 0;
    ButtonAnalogType analog_type = ButtonAnalogType::NONE;  // hat-as-button direction

    bool isSet()      const { return (!device_path.empty() && button_idx >= 0) || vk_code != 0; }
    bool isKeyboard() const { return vk_code != 0; }

    void clear() {
        clearBase();
        button_idx  = -1;
        vk_code     = 0;
        analog_type = ButtonAnalogType::NONE;
    }

    nlohmann::json   toJson()  const;
    static ButtonBinding fromJson(const nlohmann::json& j);

    static ButtonBinding fromCapture(const CaptureResult& r) {
        ButtonBinding b;
        b.device_path  = r.path;
        b.button_idx   = r.button_idx;
        b.device_name  = r.device_name;
        b.analog_type  = r.analog_type;
        return b;
    }
};

struct AnalogBinding : BindingBase {
    int           axis_idx = -1;
    bool          reverse  = false;
    ButtonBinding vtt_plus;
    ButtonBinding vtt_minus;
    int           vtt_step = 3;

    bool isSet()   const { return !device_path.empty() && axis_idx >= 0; }
    bool hasVtt()  const { return vtt_plus.isSet() || vtt_minus.isSet(); }

    void clear() {
        clearBase();
        axis_idx = -1;
        reverse  = false;
        vtt_plus.clear();
        vtt_minus.clear();
        vtt_step = 3;
    }

    nlohmann::json   toJson()  const;
    static AnalogBinding fromJson(const nlohmann::json& j);
};

struct LightBinding : BindingBase {
    int output_idx = -1;   // flat index: button_output_caps first, value_output_caps after

    bool isSet() const { return !device_path.empty() && output_idx >= 0; }

    void clear() {
        clearBase();
        output_idx = -1;
    }

    nlohmann::json toJson() const;
    static LightBinding fromJson(const nlohmann::json& j);
};

struct BindingStore {
    static constexpr int BUTTON_COUNT = static_cast<int>(DJButton::COUNT);
    static constexpr int DANCER_COUNT = static_cast<int>(DancerButton::COUNT);
    static constexpr int ANALOG_COUNT = static_cast<int>(Analog::COUNT);
    static constexpr int LIGHT_COUNT  = static_cast<int>(Light::COUNT);

    ButtonBinding buttons[BUTTON_COUNT];
    ButtonBinding dancerButtons[DANCER_COUNT];
    AnalogBinding analogs[ANALOG_COUNT];
    LightBinding  lights[LIGHT_COUNT];

    InputManager* mgr = nullptr;

    void load(SettingsManager& settings, InputManager& mgr);
    void save(SettingsManager& settings) const;

    bool isHeld(const ButtonBinding& b) const;

    using DeviceSnapshotMap = std::unordered_map<std::string, DeviceSnapshot>;
    bool    isHeldSnapshot(const ButtonBinding& b, const DeviceSnapshotMap& deviceSnapshots) const;
    uint8_t getPositionSnapshot(const AnalogBinding& a, uint8_t vtt_pos, const DeviceSnapshotMap& deviceSnapshots) const;

    std::string getDisplayString(const ButtonBinding& b) const;
    std::string getDisplayString(const AnalogBinding& a) const;
    std::string getDisplayString(const LightBinding& l) const;

    uint8_t getAnalogPosition(const AnalogBinding& a, uint8_t vtt_pos) const;
};
