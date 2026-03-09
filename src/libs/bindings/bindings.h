#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "../input/input_manager.h"   // InputManager, Device, CaptureResult
#include "../settings/settings.h"    // SettingsManager
#include <nlohmann/json.hpp>
#include <string>
#include <array>
#include <vector>

// JSON key for analog port (matches CONTEXT.md schema)
static inline const char* analogPortKey(int port) {
    return port == 0 ? "p1_turntable" : "p2_turntable";
}

// ---- ButtonBinding -------------------------------------------------------

struct ButtonBinding {
    // HID button: device_path + button_idx (device_path non-empty, vk_code==0)
    // Keyboard:   vk_code != 0 (device_path empty)
    // Unbound:    device_path empty AND vk_code == 0
    std::string      device_path;    // full Windows device path from InputManager::getDevices()[i].path
    int              button_idx = -1;
    int              vk_code    = 0;
    std::string      device_name;    // informational: for [Disconnected] display fallback
    ButtonAnalogType analog_type = ButtonAnalogType::NONE;  // hat-as-button direction

    bool isSet()      const { return (!device_path.empty() && button_idx >= 0) || vk_code != 0; }
    bool isKeyboard() const { return vk_code != 0; }

    void clear() {
        device_path.clear();
        button_idx = -1;
        vk_code    = 0;
        device_name.clear();
        analog_type = ButtonAnalogType::NONE;
    }

    nlohmann::json   toJson()  const;
    static ButtonBinding fromJson(const nlohmann::json& j);

    // Construct from CaptureResult (filled by InputManager::pollCapture())
    static ButtonBinding fromCapture(const CaptureResult& r) {
        ButtonBinding b;
        b.device_path  = r.path;
        b.button_idx   = r.button_idx;
        b.device_name  = r.device_name;
        b.analog_type  = r.analog_type;
        return b;
    }
};

// ---- AnalogBinding -------------------------------------------------------

struct AnalogBinding {
    std::string device_path;
    int         axis_idx    = -1;
    std::string device_name;
    bool        reverse     = false;
    ButtonBinding vtt_plus;
    ButtonBinding vtt_minus;
    int         vtt_step    = 3;

    bool isSet()   const { return !device_path.empty() && axis_idx >= 0; }
    bool hasVtt()  const { return vtt_plus.isSet() || vtt_minus.isSet(); }

    void clear() {
        device_path.clear();
        axis_idx = -1;
        device_name.clear();
        reverse = false;
        vtt_plus.clear();
        vtt_minus.clear();
        vtt_step = 3;
    }

    void clearAxis() {
        device_path.clear();
        axis_idx = -1;
        device_name.clear();
    }

    nlohmann::json   toJson()  const;
    static AnalogBinding fromJson(const nlohmann::json& j);
};

// ---- LightBinding --------------------------------------------------------

struct LightBinding {
    std::string device_path;   // full Windows device path (same as ButtonBinding)
    int         output_idx = -1;   // flat index: button_output_caps first, value_output_caps after
    std::string device_name;   // informational; for [Disconnected] fallback display

    bool isSet() const { return !device_path.empty() && output_idx >= 0; }

    void clear() {
        device_path.clear();
        output_idx = -1;
        device_name.clear();
    }

    nlohmann::json toJson() const;
    static LightBinding fromJson(const nlohmann::json& j);
};

// ---- BindingStore --------------------------------------------------------

// Owns all binding arrays and the InputManager reference used to query them.
// Serializes to/from globalSettings() JSON.
struct BindingStore {
    static constexpr int BUTTON_COUNT = 20;   // djButtonNames[] length
    static constexpr int DANCER_COUNT = 16;   // dancerButtonNames[] length
    static constexpr int ANALOG_COUNT = 2;    // p1_turntable + p2_turntable
    static constexpr int LIGHT_COUNT  = 23;   // lightNames[] length in strings.h

    std::array<ButtonBinding, BUTTON_COUNT> buttons;
    std::array<ButtonBinding, DANCER_COUNT> dancerButtons;
    std::array<AnalogBinding, ANALOG_COUNT> analogs;
    std::array<LightBinding,  LIGHT_COUNT>  lights;

    InputManager* mgr = nullptr;

    // Load from globalSettings(). Sets mgr for subsequent queries.
    // Old-format entries (device_id key) silently skipped.
    void load(SettingsManager& settings, InputManager& mgr);

    // Save to globalSettings() and call settings.save().
    void save(SettingsManager& settings) const;

    // Input queries — use these instead of calling InputManager directly.
    // Returns false if b is unset or mgr is null.
    bool isHeld(const ButtonBinding& b) const;

    // Returns display strings for UI rendering.
    std::string getDisplayString(const ButtonBinding& b) const;
    std::string getDisplayString(const AnalogBinding& a) const;
    std::string getDisplayString(const LightBinding& l) const;

    // Returns turntable position [0,255] incorporating axis and VTT offset.
    uint8_t getPosition(const AnalogBinding& a, uint8_t vtt_pos) const;
};
