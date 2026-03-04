#pragma once
#include "../libs/input/input_manager.h"  // InputManager, Device, CaptureResult
#include "../libs/settings/settings.h"    // SettingsManager
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
    std::string device_path;    // full Windows device path from InputManager::getDevices()[i].path
    int         button_idx = -1;
    int         vk_code    = 0;
    std::string device_name;    // informational: for [Disconnected] display fallback

    bool isSet()      const { return (!device_path.empty() && button_idx >= 0) || vk_code != 0; }
    bool isKeyboard() const { return vk_code != 0; }

    void clear() {
        device_path.clear();
        button_idx = -1;
        vk_code    = 0;
        device_name.clear();
    }

    // Returns: "Button 3 [EZ2CATCH USB]" or "[Disconnected] EZ2CATCH USB" or "Key: A" or "(unbound)"
    std::string getDisplayString(const InputManager& mgr) const;

    nlohmann::json   toJson()  const;
    static ButtonBinding fromJson(const nlohmann::json& j);

    // Construct from CaptureResult (filled by InputManager::pollCapture())
    static ButtonBinding fromCapture(const CaptureResult& r) {
        ButtonBinding b;
        b.device_path  = r.path;
        b.button_idx   = r.button_idx;
        b.device_name  = r.device_name;
        return b;
    }
};

// ---- AnalogBinding -------------------------------------------------------

struct AnalogBinding {
    std::string device_path;
    int         axis_idx    = -1;
    std::string device_name;
    bool        reverse     = false;
    float       sensitivity = 1.0f;
    float       dead_zone   = 0.04f;
    int         vtt_plus_vk  = 0;
    int         vtt_minus_vk = 0;
    int         vtt_step     = 3;

    bool isSet()   const { return !device_path.empty() && axis_idx >= 0; }
    bool hasVtt()  const { return vtt_plus_vk != 0 || vtt_minus_vk != 0; }

    void clear() {
        device_path.clear();
        axis_idx = -1;
        device_name.clear();
        reverse = false;
        sensitivity = 1.0f;
        dead_zone   = 0.04f;
        vtt_plus_vk  = 0;
        vtt_minus_vk = 0;
        vtt_step     = 3;
    }

    void clearAxis() {
        device_path.clear();
        axis_idx = -1;
        device_name.clear();
    }

    // Returns: "EZ2CATCH USB / X Axis" or "[Disconnected] EZ2CATCH USB" or "(unbound)"
    std::string getDisplayString(const InputManager& mgr) const;

    nlohmann::json   toJson()  const;
    static AnalogBinding fromJson(const nlohmann::json& j);
};

// ---- BindingStore --------------------------------------------------------

// Owns all binding arrays. Serializes to/from globalSettings() JSON.
// Does NOT #include strings.h — receives name arrays as parameters.
struct BindingStore {
    static constexpr int BUTTON_COUNT = 24;   // ioButtons[] length
    static constexpr int DANCER_COUNT = 16;   // ez2DancerIOButtons[] length
    static constexpr int ANALOG_COUNT = 2;    // p1_turntable + p2_turntable

    std::array<ButtonBinding, BUTTON_COUNT> buttons;
    std::array<ButtonBinding, DANCER_COUNT> dancerButtons;
    std::array<AnalogBinding, ANALOG_COUNT> analogs;

    // Load from globalSettings(). Old-format entries (device_id key) silently skipped.
    // ioButtonNames = ioButtons[] pointer, dancerButtonNames = ez2DancerIOButtons[]
    void load(SettingsManager& settings,
              InputManager& mgr,
              const char* const* ioButtonNames, int ioCount,
              const char* const* dancerButtonNames, int dancerCount);

    // Save to globalSettings() and call settings.save().
    void save(SettingsManager& settings,
              const char* const* ioButtonNames, int ioCount,
              const char* const* dancerButtonNames, int dancerCount) const;
};
