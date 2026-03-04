#pragma once
#include "../libs/input/input.h"    // Input::DeviceDesc, Input::CaptureResult, Input::setVttKeys
#include "../libs/settings/settings.h"  // SettingsManager
#include <string>
#include <array>
#include <vector>
#include <nlohmann/json.hpp>

// Button binding: HID button OR keyboard key
struct ButtonBinding {
    std::string device_id;    // "VID_XXXX&PID_XXXX&Instance_Y"; empty = unbound for HID
    int         button_idx = -1;  // flat index into DeviceDesc::button_labels
    int         vk_code    = 0;   // non-zero = keyboard binding (device_id unused)
    std::string device_name;      // informational: for display fallback only

    bool isSet() const { return (!device_id.empty() && button_idx >= 0) || vk_code != 0; }
    bool isKeyboard() const { return vk_code != 0; }
    void clear() { device_id.clear(); button_idx = -1; vk_code = 0; device_name.clear(); }

    // Display string: "Button 3 [EZ2CATCH USB]" or "Key: A" or "(unbound)"
    std::string getDisplayString(const std::vector<Input::DeviceDesc>& devices) const;

    nlohmann::json toJson() const;
    static ButtonBinding fromJson(const nlohmann::json& j);
    // Construct from CaptureResult
    static ButtonBinding fromCapture(const Input::CaptureResult& r);
};

// Analog (turntable) binding
struct AnalogBinding {
    std::string device_id;
    int         axis_idx    = -1;
    std::string device_name;
    bool        reverse     = false;
    float       sensitivity = 1.0f;
    float       dead_zone   = 0.04f;
    int         vtt_plus_vk  = 0;
    int         vtt_minus_vk = 0;
    int         vtt_step     = 3;

    bool isSet() const { return !device_id.empty() && axis_idx >= 0; }
    bool hasVtt() const { return vtt_plus_vk != 0 || vtt_minus_vk != 0; }
    void clearAxis() { device_id.clear(); axis_idx = -1; device_name.clear(); }

    // Display string: "EZ2CATCH USB / X Axis" or "(unbound)"
    std::string getDisplayString(const std::vector<Input::DeviceDesc>& devices) const;

    nlohmann::json toJson() const;
    static AnalogBinding fromJson(const nlohmann::json& j);
};

// Owns all binding arrays; serializes to/from globalSettings() JSON
struct BindingStore {
    // Max sizes match strings.h arrays; bindings.h does NOT include strings.h
    static constexpr int BUTTON_COUNT = 24;   // ioButtons[] length
    static constexpr int DANCER_COUNT = 16;   // ez2DancerIOButtons[] length
    static constexpr int ANALOG_COUNT = 2;    // analogs[] length

    std::array<ButtonBinding, BUTTON_COUNT> buttons;
    std::array<ButtonBinding, DANCER_COUNT> dancerButtons;
    std::array<AnalogBinding, ANALOG_COUNT> analogs;

    // Load from globalSettings(). Old-format entries (vendor_id/usage_page) are silently skipped.
    // Caller passes button name arrays so bindings.h has no dependency on strings.h.
    void load(SettingsManager& settings,
              const char* const* ioButtonNames,       // ioButtons[] pointer
              const char* const* dancerButtonNames,   // ez2DancerIOButtons[] pointer
              int ioCount, int dancerCount);

    // Save to globalSettings() and call settings.save().
    // analog_bindings keyed by "0"/"1" (port integer), NOT "P1 Turntable"/"P2 Turntable".
    void save(SettingsManager& settings,
              const char* const* ioButtonNames,
              const char* const* dancerButtonNames,
              int ioCount, int dancerCount) const;
};
