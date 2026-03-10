#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "bindings.h"
#include "game_defs.h"
#include <algorithm>
#include <cstring>

// ---- Helpers -------------------------------------------------------

// Truncate a UTF-8/ASCII string to maxLen chars for display.
static std::string truncate(const std::string& s, size_t maxLen) {
    if (s.size() <= maxLen) return s;
    return s.substr(0, maxLen);
}

// Map a virtual key code to a short display string like "A", "F5", "Enter".
static std::string vkToName(int vk) {
    UINT scanCode = MapVirtualKeyA(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
    if (scanCode == 0) return std::string("VK ") + std::to_string(vk);
    // Extended keys need bit 24 set in the lParam passed to GetKeyNameTextA.
    // For simplicity use the extended flag for a subset of known extended keys.
    bool extended = (vk == VK_INSERT || vk == VK_DELETE ||
                     vk == VK_HOME   || vk == VK_END    ||
                     vk == VK_PRIOR  || vk == VK_NEXT   ||
                     vk == VK_UP     || vk == VK_DOWN   ||
                     vk == VK_LEFT   || vk == VK_RIGHT  ||
                     vk == VK_NUMLOCK || vk == VK_RCONTROL || vk == VK_RMENU ||
                     vk == VK_DIVIDE || vk == VK_SNAPSHOT);
    LONG lParam = (scanCode << 16) | (extended ? (1 << 24) : 0);
    char buf[64] = {};
    int len = GetKeyNameTextA(lParam, buf, (int)sizeof(buf));
    if (len > 0) return std::string(buf);
    return std::string("VK ") + std::to_string(vk);
}

// Hat-switch direction check: returns true if hat_val matches the given direction.
// hat_val is a normalized [0,1] axis value; directions are evenly spaced at 1/7 intervals.
static const float HAT_SWITCH_INCREMENT = 1.0f / 7.0f;

static bool isHatDirectionActive(float hat_val, ButtonAnalogType dir) {
    if (hat_val < 0.0f) return false;
    int dir_idx = (int)dir - (int)ButtonAnalogType::HS_UP;
    float target = dir_idx * HAT_SWITCH_INCREMENT;
    float diff = hat_val - target;
    if (diff > 0.5f) diff -= 1.0f;
    if (diff < -0.5f) diff += 1.0f;
    return (diff >= -HAT_SWITCH_INCREMENT * 0.5f - 0.001f &&
            diff <=  HAT_SWITCH_INCREMENT * 0.5f + 0.001f);
}

// ---- ButtonBinding implementations ----------------------------------

nlohmann::json ButtonBinding::toJson() const {
    nlohmann::json j;
    if (isKeyboard()) {
        j["type"] = "Keyboard";
        j["vk_code"] = vk_code;
    } else {
        j["type"] = "HidButton";
        j["device_path"] = device_path;
        j["device_name"] = device_name;
        j["button_idx"]  = button_idx;
    }
    // Serialize analog_type only when non-default (keeps JSON clean for regular buttons).
    if (analog_type != ButtonAnalogType::NONE) {
        j["analog_type"] = (int)analog_type;
    }
    return j;
}

ButtonBinding ButtonBinding::fromJson(const nlohmann::json& j) {
    try {
        ButtonBinding b;
        if (!j.is_object()) return b;

        std::string type = j.value("type", "");
        if (type == "Keyboard") {
            b.vk_code = j.value("vk_code", 0);
        } else if (type == "HidButton") {
            // New format requires device_path key. Old format used device_id / vendor_id — skip.
            if (!j.contains("device_path")) return b;
            b.device_path  = j.value("device_path", "");
            b.device_name  = j.value("device_name", "");
            b.button_idx   = j.value("button_idx", -1);
        } else {
            // Unknown type — return empty binding silently
            return b;
        }

        // Deserialize analog_type (default NONE if missing — backward compat).
        b.analog_type = (ButtonAnalogType)j.value("analog_type", 0);

        return b;
    } catch (...) {
        return ButtonBinding{};
    }
}

// ---- AnalogBinding implementations ----------------------------------

nlohmann::json AnalogBinding::toJson() const {
    nlohmann::json j;
    j["device_path"]  = device_path;
    j["device_name"]  = device_name;
    j["axis_idx"]     = axis_idx;
    j["reverse"]      = reverse;
    if (vtt_plus.isSet() || vtt_minus.isSet()) {
        j["vtt"]["plus"]  = vtt_plus.toJson();
        j["vtt"]["minus"] = vtt_minus.toJson();
        j["vtt"]["step"]  = vtt_step;
    }
    return j;
}

AnalogBinding AnalogBinding::fromJson(const nlohmann::json& j) {
    try {
        AnalogBinding a;
        if (!j.is_object()) return a;

        // New format requires device_path. Old format used device_id key — skip.
        if (!j.contains("device_path")) return a;

        a.device_path  = j.value("device_path", "");
        a.device_name  = j.value("device_name", "");
        a.axis_idx     = j.value("axis_idx", -1);
        a.reverse      = j.value("reverse", false);

        if (j.contains("vtt") && j["vtt"].is_object()) {
            const auto& vtt = j["vtt"];
            a.vtt_step = vtt.value("step", 3);

            if (vtt.contains("plus") && vtt["plus"].is_object())
                a.vtt_plus  = ButtonBinding::fromJson(vtt["plus"]);
            if (vtt.contains("minus") && vtt["minus"].is_object())
                a.vtt_minus = ButtonBinding::fromJson(vtt["minus"]);
        }
        return a;
    } catch (...) {
        return AnalogBinding{};
    }
}

// ---- LightBinding implementations -----------------------------------

nlohmann::json LightBinding::toJson() const {
    nlohmann::json j;
    j["device_path"] = device_path;
    j["device_name"] = device_name;
    j["output_idx"]  = output_idx;
    return j;
}

LightBinding LightBinding::fromJson(const nlohmann::json& j) {
    LightBinding b;
    if (!j.is_object()) return b;
    b.device_path = j.value("device_path", "");
    b.device_name = j.value("device_name", "");
    b.output_idx  = j.value("output_idx", -1);
    return b;
}

// ---- BindingStore implementations -----------------------------------

void BindingStore::load(SettingsManager& settings, InputManager& inputMgr) {
    mgr = &inputMgr;

    auto& gs = settings.globalSettings();
    // io and dancer bindings are stored in separate sub-objects to avoid key
    // collisions ("Test", "Service" appear in both name arrays).
    if (gs.contains("io_button_bindings") && gs["io_button_bindings"].is_object()) {
        const auto& bb = gs["io_button_bindings"];
        for (int i = 0; i < BUTTON_COUNT; ++i) {
            if (!bb.contains(djButtonNames[i])) continue;
            const auto& val = bb[djButtonNames[i]];
            if (val.is_object())
                buttons[i] = ButtonBinding::fromJson(val);
        }
    }
    if (gs.contains("dancer_button_bindings") && gs["dancer_button_bindings"].is_object()) {
        const auto& bb = gs["dancer_button_bindings"];
        for (int i = 0; i < DANCER_COUNT; ++i) {
            if (!bb.contains(dancerButtonNames[i])) continue;
            const auto& val = bb[dancerButtonNames[i]];
            if (val.is_object())
                dancerButtons[i] = ButtonBinding::fromJson(val);
        }
    }

    if (gs.contains("analog_bindings") && gs["analog_bindings"].is_object()) {
        const auto& ab = gs["analog_bindings"];
        for (int p = 0; p < ANALOG_COUNT; ++p) {
            const char* key = analogPortKey(p);
            if (ab.contains(key)) {
                analogs[p] = AnalogBinding::fromJson(ab[key]);
            }
        }
    }

    // Load light_bindings
    if (gs.contains("light_bindings") && gs["light_bindings"].is_object()) {
        const auto& lb = gs["light_bindings"];
        for (int i = 0; i < LIGHT_COUNT; ++i) {
            if (!lb.contains(lightNames[i])) continue;
            const auto& val = lb[lightNames[i]];
            if (val.is_object())
                lights[i] = LightBinding::fromJson(val);
        }
    }

    // Configure VTT in InputManager for keyboard-only VTT bindings.
    // HID-button VTT is polled in main.cpp render loop each frame instead.
    for (int p = 0; p < ANALOG_COUNT; ++p) {
        if (analogs[p].vtt_plus.vk_code != 0 || analogs[p].vtt_minus.vk_code != 0) {
            mgr->setVttKeys(p,
                analogs[p].vtt_plus.vk_code,
                analogs[p].vtt_minus.vk_code,
                analogs[p].vtt_step);
        }
    }
}

void BindingStore::save(SettingsManager& settings) const {
    nlohmann::json& gs = settings.globalSettings();

    gs["io_button_bindings"] = nlohmann::json::object();
    for (int i = 0; i < BUTTON_COUNT; ++i) {
        if (buttons[i].isSet())
            gs["io_button_bindings"][djButtonNames[i]] = buttons[i].toJson();
    }
    gs["dancer_button_bindings"] = nlohmann::json::object();
    for (int i = 0; i < DANCER_COUNT; ++i) {
        if (dancerButtons[i].isSet())
            gs["dancer_button_bindings"][dancerButtonNames[i]] = dancerButtons[i].toJson();
    }

    gs["analog_bindings"] = nlohmann::json::object();
    for (int p = 0; p < ANALOG_COUNT; ++p) {
        if (analogs[p].isSet() || analogs[p].hasVtt()) {
            gs["analog_bindings"][analogPortKey(p)] = analogs[p].toJson();
        }
    }

    // Serialize light_bindings
    gs["light_bindings"] = nlohmann::json::object();
    for (int i = 0; i < LIGHT_COUNT; ++i) {
        if (lights[i].isSet())
            gs["light_bindings"][lightNames[i]] = lights[i].toJson();
    }

    settings.save();
}

bool BindingStore::isHeld(const ButtonBinding& b) const {
    if (!b.isSet()) return false;
    if (b.isKeyboard()) return (GetAsyncKeyState(b.vk_code) & 0x8000) != 0;
    if (!mgr) return false;
    if (b.analog_type != ButtonAnalogType::NONE) {
        float hat_val = mgr->getAxisValue(b.device_path, b.button_idx);
        return isHatDirectionActive(hat_val, b.analog_type);
    }
    return mgr->getButtonState(b.device_path, b.button_idx);
}

std::string BindingStore::getDisplayString(const ButtonBinding& b) const {
    if (!b.isSet()) return "(unbound)";

    if (b.isKeyboard()) {
        return std::string("Key: ") + vkToName(b.vk_code);
    }

    if (!mgr) return std::string("[Disconnected] ") + truncate(b.device_name, 18);

    // HID path
    std::vector<Device> devs = mgr->getDevices();
    for (const Device& dev : devs) {
        if (dev.path == b.device_path) {
            std::string label;
            if (b.button_idx >= 0 && b.button_idx < (int)dev.button_caps_names.size()) {
                label = dev.button_caps_names[b.button_idx];
            } else {
                label = std::string("Button ") + std::to_string(b.button_idx);
            }
            std::string devName = truncate(dev.name, 18);
            return label + " [" + devName + "]";
        }
    }
    // Device not found — disconnected fallback
    return std::string("[Disconnected] ") + truncate(b.device_name, 18);
}

std::string BindingStore::getDisplayString(const AnalogBinding& a) const {
    if (!a.isSet()) return "(unbound)";

    if (!mgr) return std::string("[Disconnected] ") + a.device_name;

    std::vector<Device> devs = mgr->getDevices();
    for (const Device& dev : devs) {
        if (dev.path == a.device_path) {
            std::string axisLabel;
            if (a.axis_idx >= 0 && a.axis_idx < (int)dev.value_caps_names.size()) {
                axisLabel = dev.value_caps_names[a.axis_idx];
            } else {
                axisLabel = std::string("Axis ") + std::to_string(a.axis_idx);
            }
            return dev.name + " / " + axisLabel;
        }
    }
    return std::string("[Disconnected] ") + a.device_name;
}

std::string BindingStore::getDisplayString(const LightBinding& l) const {
    if (!l.isSet()) return "(unbound)";

    if (!mgr) return std::string("[Disconnected] ") + l.device_name;

    std::vector<Device> devs = mgr->getDevices();
    for (const auto& dev : devs) {
        if (dev.path != l.device_path) continue;
        int btn_count = (int)dev.button_output_caps_names.size();
        if (l.output_idx < btn_count)
            return dev.button_output_caps_names[l.output_idx] + " (" + dev.name + ")";
        int val_idx = l.output_idx - btn_count;
        if (val_idx < (int)dev.value_output_caps_names.size())
            return dev.value_output_caps_names[val_idx] + " (" + dev.name + ")";
        return "Output " + std::to_string(l.output_idx) + " (" + dev.name + ")";
    }
    return "[Disconnected] " + l.device_name;
}

uint8_t BindingStore::getPosition(const AnalogBinding& a, uint8_t vtt_pos) const {
    if (!a.isSet() || !mgr) return vtt_pos;
    float raw = mgr->getAxisValue(a.device_path, a.axis_idx);
    if (a.reverse) raw = 1.0f - raw;
    return (uint8_t)((int)(raw * 255.0f) + (int)vtt_pos - 128);
}

bool BindingStore::isHeldSnapshot(const ButtonBinding& b, const SnapMap& snap) const {
    if (!b.isSet()) return false;
    if (b.isKeyboard()) return (GetAsyncKeyState(b.vk_code) & 0x8000) != 0;
    auto it = snap.find(b.device_path);
    if (it == snap.end()) return false;
    const DeviceSnapshot& ds = it->second;
    if (b.analog_type != ButtonAnalogType::NONE) {
        if (b.button_idx < 0 || b.button_idx >= (int)ds.values.size()) return false;
        return isHatDirectionActive(ds.values[b.button_idx], b.analog_type);
    }
    if (b.button_idx < 0 || b.button_idx >= (int)ds.buttons.size()) return false;
    return ds.buttons[b.button_idx];
}

uint8_t BindingStore::getPositionSnapshot(const AnalogBinding& a, uint8_t vtt_pos, const SnapMap& snap) const {
    if (!a.isSet()) return vtt_pos;
    float raw = 0.5f;
    auto it = snap.find(a.device_path);
    if (it != snap.end() && a.axis_idx >= 0 && a.axis_idx < (int)it->second.values.size())
        raw = it->second.values[a.axis_idx];
    if (a.reverse) raw = 1.0f - raw;
    return (uint8_t)((int)(raw * 255.0f) + (int)vtt_pos - 128);
}
