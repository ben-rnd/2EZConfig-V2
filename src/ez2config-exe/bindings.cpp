#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "bindings.h"
#include <cstdio>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// ButtonBinding
// ---------------------------------------------------------------------------

std::string ButtonBinding::getDisplayString(const std::vector<Input::DeviceDesc>& devices) const {
    if (!isSet()) return "(unbound)";

    if (isKeyboard()) {
        char buf[64] = {};
        UINT sc = MapVirtualKeyA((UINT)vk_code, MAPVK_VK_TO_VSC);
        GetKeyNameTextA((LONG)(sc << 16), buf, sizeof(buf));
        return std::string("Key: ") + (buf[0] ? buf : "?");
    }

    // HID button — find device in list for label
    for (const auto& dev : devices) {
        if (dev.id == device_id) {
            std::string label;
            if (button_idx >= 0 && button_idx < (int)dev.button_labels.size()) {
                label = dev.button_labels[(size_t)button_idx];
            } else {
                char fallback[32];
                snprintf(fallback, sizeof(fallback), "Button %d", button_idx + 1);
                label = fallback;
            }
            std::string display_name = dev.name;
            if (display_name.size() > 18) display_name = display_name.substr(0, 18);
            return label + " [" + display_name + "]";
        }
    }

    // Device not found — use fallback
    std::string fallback_name = !device_name.empty() ? device_name : device_id;
    if (fallback_name.size() > 18) fallback_name = fallback_name.substr(0, 18);
    char fallback_label[32];
    snprintf(fallback_label, sizeof(fallback_label), "Button %d", button_idx + 1);
    return std::string(fallback_label) + " [" + fallback_name + "]";
}

nlohmann::json ButtonBinding::toJson() const {
    if (isKeyboard()) {
        return nlohmann::json{{"type", "Keyboard"}, {"vk_code", vk_code}};
    }
    return nlohmann::json{
        {"type", "HidButton"},
        {"device_id", device_id},
        {"button_idx", button_idx},
        {"device_name", device_name}
    };
}

ButtonBinding ButtonBinding::fromJson(const nlohmann::json& j) {
    ButtonBinding b;
    try {
        if (!j.is_object()) return b;
        std::string btype = j.value("type", std::string());
        if (btype == "Keyboard") {
            b.vk_code = j.value("vk_code", 0);
            return b;
        }
        if (btype == "HidButton") {
            // New format: device_id + button_idx
            if (j.contains("device_id")) {
                b.device_id   = j.value("device_id", std::string());
                b.button_idx  = j.value("button_idx", -1);
                b.device_name = j.value("device_name", std::string());
                return b;
            }
            // Old format: vendor_id/product_id/usage_page/usage_id — silently skip
            return b;  // returns empty (unset) binding
        }
    } catch (...) {}
    return b;
}

ButtonBinding ButtonBinding::fromCapture(const Input::CaptureResult& r) {
    ButtonBinding b;
    b.device_id   = r.device_id;
    b.button_idx  = r.button_idx;
    b.device_name = r.device_name;
    return b;
}

// ---------------------------------------------------------------------------
// AnalogBinding
// ---------------------------------------------------------------------------

std::string AnalogBinding::getDisplayString(const std::vector<Input::DeviceDesc>& devices) const {
    if (!isSet()) return "(unbound)";

    std::string axis_label;
    std::string display_name;

    for (const auto& dev : devices) {
        if (dev.id == device_id) {
            display_name = dev.name;
            if (axis_idx >= 0 && axis_idx < (int)dev.axis_labels.size()) {
                axis_label = dev.axis_labels[(size_t)axis_idx];
            }
            break;
        }
    }

    if (display_name.empty()) {
        display_name = !device_name.empty() ? device_name : device_id;
    }
    if (axis_label.empty()) {
        char fallback[24];
        snprintf(fallback, sizeof(fallback), "Axis %d", axis_idx);
        axis_label = fallback;
    }

    return display_name + " / " + axis_label;
}

nlohmann::json AnalogBinding::toJson() const {
    nlohmann::json j;
    j["device_id"]   = device_id;
    j["axis_idx"]    = axis_idx;
    j["device_name"] = device_name;
    j["reverse"]     = reverse;
    j["sensitivity"] = sensitivity;
    j["dead_zone"]   = dead_zone;
    if (vtt_plus_vk != 0 || vtt_minus_vk != 0) {
        j["vtt"] = nlohmann::json{
            {"plus_vk",  vtt_plus_vk},
            {"minus_vk", vtt_minus_vk},
            {"step",     vtt_step}
        };
    }
    return j;
}

AnalogBinding AnalogBinding::fromJson(const nlohmann::json& j) {
    AnalogBinding a;
    try {
        if (!j.is_object()) return a;
        // New format requires device_id key; old format used vendor_id — silently skip old
        if (!j.contains("device_id")) return a;
        a.device_id   = j.value("device_id",   std::string());
        a.axis_idx    = j.value("axis_idx",     -1);
        a.device_name = j.value("device_name",  std::string());
        a.reverse     = j.value("reverse",      false);
        a.sensitivity = j.value("sensitivity",  1.0f);
        a.dead_zone   = j.value("dead_zone",    0.04f);
        if (j.contains("vtt") && j["vtt"].is_object()) {
            const auto& vt = j["vtt"];
            a.vtt_plus_vk  = vt.value("plus_vk",  0);
            a.vtt_minus_vk = vt.value("minus_vk", 0);
            a.vtt_step     = vt.value("step",      3);
        }
    } catch (...) {}
    return a;
}

// ---------------------------------------------------------------------------
// BindingStore::load
// ---------------------------------------------------------------------------

void BindingStore::load(SettingsManager& settings,
                        const char* const* ioButtonNames,
                        const char* const* dancerButtonNames,
                        int ioCount, int dancerCount) {
    auto& gs = settings.globalSettings();

    // Button bindings (DJ IO)
    if (gs.contains("button_bindings")) {
        const auto& bb = gs["button_bindings"];
        for (int i = 0; i < ioCount && i < BUTTON_COUNT; i++) {
            const char* name = ioButtonNames[i];
            if (name && bb.contains(name)) {
                buttons[i] = ButtonBinding::fromJson(bb[name]);
            }
        }
        // Dancer button bindings
        for (int i = 0; i < dancerCount && i < DANCER_COUNT; i++) {
            const char* name = dancerButtonNames[i];
            if (name && bb.contains(name)) {
                dancerButtons[i] = ButtonBinding::fromJson(bb[name]);
            }
        }
    }

    // Analog bindings — keyed by "0" and "1" (port integer strings)
    if (gs.contains("analog_bindings")) {
        const auto& ab = gs["analog_bindings"];
        for (int p = 0; p < ANALOG_COUNT; p++) {
            std::string portKey = std::to_string(p);
            if (ab.contains(portKey)) {
                analogs[p] = AnalogBinding::fromJson(ab[portKey]);
            }
        }
    }

    // Configure VTT keys in input library for each bound analog
    for (int p = 0; p < ANALOG_COUNT; p++) {
        if (analogs[p].hasVtt()) {
            Input::setVttKeys(p,
                              analogs[p].vtt_plus_vk,
                              analogs[p].vtt_minus_vk,
                              analogs[p].vtt_step);
        }
    }
}

// ---------------------------------------------------------------------------
// BindingStore::save
// ---------------------------------------------------------------------------

void BindingStore::save(SettingsManager& settings,
                        const char* const* ioButtonNames,
                        const char* const* dancerButtonNames,
                        int ioCount, int dancerCount) const {
    auto& gs = settings.globalSettings();

    // Clear existing button_bindings and rewrite
    gs["button_bindings"] = nlohmann::json::object();

    for (int i = 0; i < ioCount && i < BUTTON_COUNT; i++) {
        if (buttons[i].isSet() && ioButtonNames[i]) {
            gs["button_bindings"][ioButtonNames[i]] = buttons[i].toJson();
        }
    }
    for (int i = 0; i < dancerCount && i < DANCER_COUNT; i++) {
        if (dancerButtons[i].isSet() && dancerButtonNames[i]) {
            gs["button_bindings"][dancerButtonNames[i]] = dancerButtons[i].toJson();
        }
    }

    // Analog bindings — keyed by "0"/"1" (port integer string), NEVER "P1 Turntable"
    gs["analog_bindings"] = nlohmann::json::object();
    for (int p = 0; p < ANALOG_COUNT; p++) {
        if (analogs[p].isSet()) {
            gs["analog_bindings"][std::to_string(p)] = analogs[p].toJson();
        }
    }

    settings.save();
}
