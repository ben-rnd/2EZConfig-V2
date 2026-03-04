#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "bindings.h"
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

// ---- ButtonBinding implementations ----------------------------------

std::string ButtonBinding::getDisplayString(const InputManager& mgr) const {
    if (!isSet()) return "(unbound)";

    if (isKeyboard()) {
        return std::string("Key: ") + vkToName(vk_code);
    }

    // HID path
    std::vector<Device> devs = mgr.getDevices();
    for (const Device& dev : devs) {
        if (dev.path == device_path) {
            // Found — get button label
            std::string label;
            if (button_idx >= 0 && button_idx < (int)dev.button_caps_names.size()) {
                label = dev.button_caps_names[button_idx];
            } else {
                label = std::string("Button ") + std::to_string(button_idx);
            }
            std::string devName = truncate(dev.name, 18);
            return label + " [" + devName + "]";
        }
    }
    // Device not found — disconnected fallback
    return std::string("[Disconnected] ") + truncate(device_name, 18);
}

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
    return j;
}

/*static*/ ButtonBinding ButtonBinding::fromJson(const nlohmann::json& j) {
    try {
        ButtonBinding b;
        if (!j.is_object()) return b;

        std::string type = j.value("type", "");
        if (type == "Keyboard") {
            b.vk_code = j.value("vk_code", 0);
            return b;
        }
        if (type == "HidButton") {
            // New format requires device_path key. Old format used device_id / vendor_id — skip.
            if (!j.contains("device_path")) return b;
            b.device_path  = j.value("device_path", "");
            b.device_name  = j.value("device_name", "");
            b.button_idx   = j.value("button_idx", -1);
            return b;
        }
        // Unknown type — return empty binding silently
        return b;
    } catch (...) {
        return ButtonBinding{};
    }
}

// ---- AnalogBinding implementations ----------------------------------

std::string AnalogBinding::getDisplayString(const InputManager& mgr) const {
    if (!isSet()) return "(unbound)";

    std::vector<Device> devs = mgr.getDevices();
    for (const Device& dev : devs) {
        if (dev.path == device_path) {
            std::string axisLabel;
            if (axis_idx >= 0 && axis_idx < (int)dev.value_caps_names.size()) {
                axisLabel = dev.value_caps_names[axis_idx];
            } else {
                axisLabel = std::string("Axis ") + std::to_string(axis_idx);
            }
            return dev.name + " / " + axisLabel;
        }
    }
    // Disconnected fallback
    return std::string("[Disconnected] ") + device_name;
}

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

/*static*/ AnalogBinding AnalogBinding::fromJson(const nlohmann::json& j) {
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

            // New format: "plus" and "minus" are VttKey objects
            if (vtt.contains("plus") && vtt["plus"].is_object()) {
                a.vtt_plus = VttKey::fromJson(vtt["plus"]);
            }
            if (vtt.contains("minus") && vtt["minus"].is_object()) {
                a.vtt_minus = VttKey::fromJson(vtt["minus"]);
            }
            // Backwards compat: old format had "plus_vk"/"minus_vk" as ints
            if (a.vtt_plus.vk == 0 && !a.vtt_plus.isSet()) {
                int old_plus = vtt.value("plus_vk", 0);
                if (old_plus != 0) a.vtt_plus.vk = old_plus;
            }
            if (a.vtt_minus.vk == 0 && !a.vtt_minus.isSet()) {
                int old_minus = vtt.value("minus_vk", 0);
                if (old_minus != 0) a.vtt_minus.vk = old_minus;
            }
        }
        return a;
    } catch (...) {
        return AnalogBinding{};
    }
}

// ---- LightBinding implementations -----------------------------------

std::string LightBinding::getDisplayString(const InputManager& mgr) const {
    if (!isSet()) return "(unbound)";
    std::vector<Device> devs = mgr.getDevices();
    for (const auto& dev : devs) {
        if (dev.path != device_path) continue;
        int btn_count = (int)dev.button_output_caps_names.size();
        if (output_idx < btn_count)
            return dev.button_output_caps_names[output_idx] + " (" + dev.name + ")";
        int val_idx = output_idx - btn_count;
        if (val_idx < (int)dev.value_output_caps_names.size())
            return dev.value_output_caps_names[val_idx] + " (" + dev.name + ")";
        return "Output " + std::to_string(output_idx) + " (" + dev.name + ")";
    }
    return "[Disconnected] " + device_name;
}

nlohmann::json LightBinding::toJson() const {
    nlohmann::json j;
    j["device_path"] = device_path;
    j["device_name"] = device_name;
    j["output_idx"]  = output_idx;
    return j;
}

/*static*/ LightBinding LightBinding::fromJson(const nlohmann::json& j) {
    LightBinding b;
    if (!j.is_object()) return b;
    b.device_path = j.value("device_path", "");
    b.device_name = j.value("device_name", "");
    b.output_idx  = j.value("output_idx", -1);
    return b;
}

// ---- BindingStore implementations -----------------------------------

void BindingStore::load(SettingsManager& settings,
                        InputManager& mgr,
                        const char* const* ioButtonNames, int ioCount,
                        const char* const* dancerButtonNames, int dancerCount,
                        const char* const* lightNames, int lightCount) {
    auto& gs = settings.globalSettings();
    if (gs.contains("button_bindings") && gs["button_bindings"].is_object()) {
        const auto& bb = gs["button_bindings"];
        for (int i = 0; i < ioCount && i < BUTTON_COUNT; ++i) {
            if (!bb.contains(ioButtonNames[i])) continue;
            const auto& val = bb[ioButtonNames[i]];
            if (val.is_array()) {
                // New format: index 0 = primary, 1-2 = alternatives
                if (!val.empty() && !val[0].is_null())
                    buttons[i] = ButtonBinding::fromJson(val[0]);
                for (size_t p = 1; p < val.size() && p <= 2; ++p) {
                    if (!val[p].is_null())
                        buttons[i].alternatives.push_back(ButtonBinding::fromJson(val[p]));
                }
            } else if (val.is_object()) {
                // Old format: single binding → page 0 only
                buttons[i] = ButtonBinding::fromJson(val);
            }
        }
        for (int i = 0; i < dancerCount && i < DANCER_COUNT; ++i) {
            if (!bb.contains(dancerButtonNames[i])) continue;
            const auto& val = bb[dancerButtonNames[i]];
            if (val.is_array()) {
                if (!val.empty() && !val[0].is_null())
                    dancerButtons[i] = ButtonBinding::fromJson(val[0]);
                for (size_t p = 1; p < val.size() && p <= 2; ++p) {
                    if (!val[p].is_null())
                        dancerButtons[i].alternatives.push_back(ButtonBinding::fromJson(val[p]));
                }
            } else if (val.is_object()) {
                dancerButtons[i] = ButtonBinding::fromJson(val);
            }
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
        for (int i = 0; i < lightCount && i < LIGHT_COUNT; ++i) {
            if (!lb.contains(lightNames[i])) continue;
            const auto& val = lb[lightNames[i]];
            if (val.is_array()) {
                if (!val.empty() && !val[0].is_null())
                    lights[i] = LightBinding::fromJson(val[0]);
                for (size_t p = 1; p < val.size() && p <= 2; ++p) {
                    if (!val[p].is_null())
                        lights[i].alternatives.push_back(LightBinding::fromJson(val[p]));
                }
            }
        }
    }

    // Configure VTT in InputManager for keyboard-only VTT bindings.
    // HID-button VTT is polled in main.cpp render loop each frame instead.
    for (int p = 0; p < ANALOG_COUNT; ++p) {
        if (analogs[p].vtt_plus.vk != 0 || analogs[p].vtt_minus.vk != 0) {
            mgr.setVttKeys(p,
                analogs[p].vtt_plus.vk,
                analogs[p].vtt_minus.vk,
                analogs[p].vtt_step);
        }
    }
}

void BindingStore::save(SettingsManager& settings,
                        const char* const* ioButtonNames, int ioCount,
                        const char* const* dancerButtonNames, int dancerCount,
                        const char* const* lightNames, int lightCount) const {
    nlohmann::json& gs = settings.globalSettings();

    gs["button_bindings"] = nlohmann::json::object();
    for (int i = 0; i < ioCount && i < BUTTON_COUNT; ++i) {
        nlohmann::json page_arr = nlohmann::json::array();
        page_arr.push_back(buttons[i].isSet() ? buttons[i].toJson() : nlohmann::json(nullptr));
        for (const auto& alt : buttons[i].alternatives) {
            page_arr.push_back(alt.isSet() ? alt.toJson() : nlohmann::json(nullptr));
        }
        // Trim trailing nulls for clean JSON
        while (!page_arr.empty() && page_arr.back().is_null()) page_arr.erase(page_arr.end() - 1);
        if (!page_arr.empty()) gs["button_bindings"][ioButtonNames[i]] = page_arr;
    }
    for (int i = 0; i < dancerCount && i < DANCER_COUNT; ++i) {
        nlohmann::json page_arr = nlohmann::json::array();
        page_arr.push_back(dancerButtons[i].isSet() ? dancerButtons[i].toJson() : nlohmann::json(nullptr));
        for (const auto& alt : dancerButtons[i].alternatives) {
            page_arr.push_back(alt.isSet() ? alt.toJson() : nlohmann::json(nullptr));
        }
        while (!page_arr.empty() && page_arr.back().is_null()) page_arr.erase(page_arr.end() - 1);
        if (!page_arr.empty()) gs["button_bindings"][dancerButtonNames[i]] = page_arr;
    }

    gs["analog_bindings"] = nlohmann::json::object();
    for (int p = 0; p < ANALOG_COUNT; ++p) {
        if (analogs[p].isSet() || analogs[p].hasVtt()) {
            gs["analog_bindings"][analogPortKey(p)] = analogs[p].toJson();
        }
    }

    // Serialize light_bindings
    nlohmann::json lb_obj = nlohmann::json::object();
    for (int i = 0; i < lightCount && i < LIGHT_COUNT; ++i) {
        const auto& lb = lights[i];
        nlohmann::json lb_arr = nlohmann::json::array();
        lb_arr.push_back(lb.isSet() ? lb.toJson() : nlohmann::json(nullptr));
        for (const auto& alt : lb.alternatives) {
            lb_arr.push_back(alt.isSet() ? alt.toJson() : nlohmann::json(nullptr));
        }
        while (!lb_arr.empty() && lb_arr.back().is_null()) lb_arr.erase(lb_arr.end() - 1);
        if (!lb_arr.empty()) lb_obj[lightNames[i]] = lb_arr;
    }
    if (!lb_obj.empty()) gs["light_bindings"] = lb_obj;

    settings.save();
}
