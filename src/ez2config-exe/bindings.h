#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
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
    std::vector<ButtonBinding> alternatives;  // max 2 entries (pages 2 and 3)

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

// ---- VttKey --------------------------------------------------------------
// Represents a single VTT direction binding: either a keyboard VK or an HID
// controller button. Exactly one of (vk != 0) or (device_path non-empty) is set.

struct VttKey {
    int         vk          = 0;    // keyboard virtual key code
    std::string device_path;        // HID device path (non-empty = controller binding)
    int         button_idx  = -1;   // HID button index within device
    std::string device_name;        // informational display fallback

    bool isSet() const { return vk != 0 || (!device_path.empty() && button_idx >= 0); }

    void clear() {
        vk = 0;
        device_path.clear();
        button_idx = -1;
        device_name.clear();
    }

    std::string getLabel() const {
        if (vk != 0) {
            UINT scanCode = MapVirtualKeyA(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
            bool extended = (vk == VK_INSERT || vk == VK_DELETE ||
                             vk == VK_HOME   || vk == VK_END    ||
                             vk == VK_PRIOR  || vk == VK_NEXT   ||
                             vk == VK_UP     || vk == VK_DOWN   ||
                             vk == VK_LEFT   || vk == VK_RIGHT);
            LONG lParam = (LONG)((scanCode << 16) | (extended ? (1 << 24) : 0));
            char buf[64] = {};
            if (GetKeyNameTextA(lParam, buf, sizeof(buf)) > 0)
                return std::string(buf);
            return "Key " + std::to_string(vk);
        }
        if (!device_path.empty() && button_idx >= 0)
            return "Btn " + std::to_string(button_idx + 1);
        return "(unbound)";
    }

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["vk"]          = vk;
        j["device_path"] = device_path;
        j["device_name"] = device_name;
        j["button_idx"]  = button_idx;
        return j;
    }

    static VttKey fromJson(const nlohmann::json& j) {
        VttKey k;
        if (!j.is_object()) return k;
        k.vk          = j.value("vk", 0);
        k.device_path = j.value("device_path", "");
        k.device_name = j.value("device_name", "");
        k.button_idx  = j.value("button_idx", -1);
        // Backwards compat: old format had "plus_vk"/"minus_vk" as direct int
        // (handled in AnalogBinding::fromJson)
        return k;
    }
};

// ---- AnalogBinding -------------------------------------------------------

struct AnalogBinding {
    std::string device_path;
    int         axis_idx    = -1;
    std::string device_name;
    bool        reverse     = false;
    VttKey      vtt_plus;
    VttKey      vtt_minus;
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

    // Returns: "EZ2CATCH USB / X Axis" or "[Disconnected] EZ2CATCH USB" or "(unbound)"
    std::string getDisplayString(const InputManager& mgr) const;

    nlohmann::json   toJson()  const;
    static AnalogBinding fromJson(const nlohmann::json& j);

    uint8_t getPosition(const InputManager& mgr, uint8_t vtt_pos) const {
        if (!isSet()) return vtt_pos;
        float raw = mgr.getAxisValue(device_path, axis_idx);
        if (reverse) raw = 1.0f - raw;
        return (uint8_t)((int)(raw * 255.0f) + (int)vtt_pos - 128);
    }
};

// ---- LightBinding --------------------------------------------------------

struct LightBinding {
    std::string device_path;   // full Windows device path (same as ButtonBinding)
    int         output_idx = -1;   // flat index: button_output_caps first, value_output_caps after
    std::string device_name;   // informational; for [Disconnected] fallback display
    std::vector<LightBinding> alternatives;  // max 2 entries (pages 2 and 3)

    bool isSet() const { return !device_path.empty() && output_idx >= 0; }

    void clear() {
        device_path.clear();
        output_idx = -1;
        device_name.clear();
        // Does NOT clear alternatives — caller decides per page
    }

    // Returns: "Button 3 (LED Controller)" or "[Disconnected] LED Controller" or "(unbound)"
    std::string getDisplayString(const InputManager& mgr) const;

    nlohmann::json toJson() const;
    static LightBinding fromJson(const nlohmann::json& j);
};

// ---- BindingStore --------------------------------------------------------

// Owns all binding arrays. Serializes to/from globalSettings() JSON.
// Does NOT #include strings.h — receives name arrays as parameters.
struct BindingStore {
    static constexpr int BUTTON_COUNT = 20;   // ioButtons[] length
    static constexpr int DANCER_COUNT = 16;   // ez2DancerIOButtons[] length
    static constexpr int ANALOG_COUNT = 2;    // p1_turntable + p2_turntable
    static constexpr int LIGHT_COUNT  = 23;   // lights[] array length in strings.h

    std::array<ButtonBinding, BUTTON_COUNT> buttons;
    std::array<ButtonBinding, DANCER_COUNT> dancerButtons;
    std::array<AnalogBinding, ANALOG_COUNT> analogs;
    std::array<LightBinding,  LIGHT_COUNT>  lights;

    // Load from globalSettings(). Old-format entries (device_id key) silently skipped.
    // ioButtonNames = ioButtons[] pointer, dancerButtonNames = ez2DancerIOButtons[]
    void load(SettingsManager& settings,
              InputManager& mgr,
              const char* const* ioButtonNames, int ioCount,
              const char* const* dancerButtonNames, int dancerCount,
              const char* const* lightNames, int lightCount);

    // Save to globalSettings() and call settings.save().
    void save(SettingsManager& settings,
              const char* const* ioButtonNames, int ioCount,
              const char* const* dancerButtonNames, int dancerCount,
              const char* const* lightNames, int lightCount) const;
};
