#include <windows.h>
#include "bindings.h"
#include "utilities.h"

static std::string vkToName(int vk) {
    UINT scanCode = MapVirtualKeyA(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
    if (scanCode == 0) {
        return std::string("VK ") + std::to_string(vk);
    }
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
    char keyNameBuffer[64] = {};
    int len = GetKeyNameTextA(lParam, keyNameBuffer, static_cast<int>(sizeof(keyNameBuffer)));
    if (len > 0) {
        return std::string(keyNameBuffer);
    }
    return std::string("VK ") + std::to_string(vk);
}

static bool isHatDirectionActive(float hatValue, ButtonAnalogType dir) {
    if (hatValue < 0.0f) {
        return false;
    }
    int directionIndex = static_cast<int>(dir) - static_cast<int>(ButtonAnalogType::HS_UP);
    float target = directionIndex * HAT_SWITCH_INCREMENT;
    float diff = hatValue - target;
    if (diff > 0.5f) {
        diff -= 1.0f;
    }
    if (diff < -0.5f) {
        diff += 1.0f;
    }
    return (diff >= -HAT_SWITCH_INCREMENT * 0.5f - 0.001f &&
            diff <=  HAT_SWITCH_INCREMENT * 0.5f + 0.001f);
}

nlohmann::json ButtonBinding::toJson() const {
    nlohmann::json j;
    if (isKeyboard()) {
        j["type"] = "Keyboard";
        j["vk_code"] = vkCode;
    } else {
        j["type"] = "HidButton";
        j["device_path"] = devicePath;
        j["device_name"] = deviceName;
        j["button_idx"]  = buttonIdx;
    }
    if (analogType != ButtonAnalogType::NONE) {
        j["analog_type"] = static_cast<int>(analogType);
    }
    return j;
}

ButtonBinding ButtonBinding::fromJson(const nlohmann::json& j) {
    try {
        ButtonBinding binding;
        if (!j.is_object()) {
            return binding;
        }

        std::string type = j.value("type", "");
        if (type == "Keyboard") {
            binding.vkCode = j.value("vk_code", 0);
        } else if (type == "HidButton") {
            if (!j.contains("device_path")) {
                return binding;
            }
            binding.devicePath  = j.value("device_path", "");
            binding.deviceName  = j.value("device_name", "");
            binding.buttonIdx   = j.value("button_idx", -1);
        } else {
            // Unknown type — return empty binding silently
            return binding;
        }

        binding.analogType = static_cast<ButtonAnalogType>(j.value("analog_type", 0));

        return binding;
    } catch (...) {
        return ButtonBinding{};
    }
}

nlohmann::json AnalogBinding::toJson() const {
    nlohmann::json j;
    j["device_path"]  = devicePath;
    j["device_name"]  = deviceName;
    j["axis_idx"]     = axisIdx;
    j["reverse"]      = reverse;
    if (vttPlus.isSet() || vttMinus.isSet()) {
        j["vtt"]["plus"]  = vttPlus.toJson();
        j["vtt"]["minus"] = vttMinus.toJson();
        j["vtt"]["step"]  = vttStep;
    }
    if (mouseAxis >= 0 && !mousePath.empty()) {
        j["mouse"]["device_path"]  = mousePath;
        j["mouse"]["device_name"]  = mouseName;
        j["mouse"]["axis"]         = mouseAxis;
        j["mouse"]["sensitivity"]  = mouseSensitivity;
    }
    return j;
}

AnalogBinding AnalogBinding::fromJson(const nlohmann::json& j) {
    try {
        AnalogBinding analogBinding;
        if (!j.is_object()) {
            return analogBinding;
        }

        // New format requires device_path. Old format used device_id key — skip.
        if (!j.contains("device_path")) {
            return analogBinding;
        }

        analogBinding.devicePath  = j.value("device_path", "");
        analogBinding.deviceName  = j.value("device_name", "");
        analogBinding.axisIdx     = j.value("axis_idx", -1);
        analogBinding.reverse      = j.value("reverse", false);

        if (j.contains("vtt") && j["vtt"].is_object()) {
            const auto& vtt = j["vtt"];
            analogBinding.vttStep = vtt.value("step", 3);

            if (vtt.contains("plus") && vtt["plus"].is_object()) {
                analogBinding.vttPlus  = ButtonBinding::fromJson(vtt["plus"]);
            }
            if (vtt.contains("minus") && vtt["minus"].is_object()) {
                analogBinding.vttMinus = ButtonBinding::fromJson(vtt["minus"]);
            }
        }
        if (j.contains("mouse") && j["mouse"].is_object()) {
            const auto& m = j["mouse"];
            analogBinding.mousePath        = m.value("device_path", "");
            analogBinding.mouseName        = m.value("device_name", "");
            analogBinding.mouseAxis        = m.value("axis", -1);
            analogBinding.mouseSensitivity = m.value("sensitivity", 5);
        }
        return analogBinding;
    } catch (...) {
        return AnalogBinding{};
    }
}

nlohmann::json LightBinding::toJson() const {
    nlohmann::json j;
    j["device_path"] = devicePath;
    j["device_name"] = deviceName;
    j["output_idx"]  = outputIdx;
    return j;
}

LightBinding LightBinding::fromJson(const nlohmann::json& j) {
    LightBinding lightBinding;
    if (!j.is_object()) {
        return lightBinding;
    }
    lightBinding.devicePath = j.value("device_path", "");
    lightBinding.deviceName = j.value("device_name", "");
    lightBinding.outputIdx  = j.value("output_idx", -1);
    return lightBinding;
}

void BindingStore::load(SettingsManager& settings, InputManager& inputMgr) {
    mgr = &inputMgr;

    auto& globalSettings = settings.globalSettings();

    if (globalSettings.contains("io_button_bindings") && globalSettings["io_button_bindings"].is_object()) {
        const auto& buttonBindingsJson = globalSettings["io_button_bindings"];
        for (int i = 0; i < BUTTON_COUNT; ++i) {
            if (!buttonBindingsJson.contains(djButtonNames[i])) {
                continue;
            }
            const auto& bindingJson = buttonBindingsJson[djButtonNames[i]];
            if (bindingJson.is_object()) {
                buttons[i] = ButtonBinding::fromJson(bindingJson);
            }
        }
    }
    if (globalSettings.contains("dancer_button_bindings") && globalSettings["dancer_button_bindings"].is_object()) {
        const auto& buttonBindingsJson = globalSettings["dancer_button_bindings"];
        for (int i = 0; i < DANCER_COUNT; ++i) {
            if (!buttonBindingsJson.contains(dancerButtonNames[i])) {
                continue;
            }
            const auto& bindingJson = buttonBindingsJson[dancerButtonNames[i]];
            if (bindingJson.is_object()) {
                dancerButtons[i] = ButtonBinding::fromJson(bindingJson);
            }
        }
    }

    if (globalSettings.contains("analog_bindings") && globalSettings["analog_bindings"].is_object()) {
        const auto& analogBindingsJson = globalSettings["analog_bindings"];
        for (int p = 0; p < ANALOG_COUNT; ++p) {
            const char* key = analogPortKey(p);
            if (analogBindingsJson.contains(key)) {
                analogs[p] = AnalogBinding::fromJson(analogBindingsJson[key]);
            }
        }
    }

    if (globalSettings.contains("light_bindings") && globalSettings["light_bindings"].is_object()) {
        const auto& lightBindingsJson = globalSettings["light_bindings"];
        for (int i = 0; i < LIGHT_COUNT; ++i) {
            if (!lightBindingsJson.contains(lightNames[i])) {
                continue;
            }
            const auto& bindingJson = lightBindingsJson[lightNames[i]];
            if (bindingJson.is_object()) {
                lights[i] = LightBinding::fromJson(bindingJson);
            }
        }
    }

    for (int p = 0; p < ANALOG_COUNT; ++p) {
        if (analogs[p].vttPlus.vkCode != 0 || analogs[p].vttMinus.vkCode != 0) {
            mgr->setVttKeys(p,
                analogs[p].vttPlus.vkCode,
                analogs[p].vttMinus.vkCode,
                analogs[p].vttStep);
        }
        if (analogs[p].hasMouse()) {
            mgr->setMouseBinding(p, analogs[p].mousePath, analogs[p].mouseAxis, analogs[p].mouseSensitivity);
        }
    }
}

void BindingStore::save(SettingsManager& settings) const {
    nlohmann::json& gs = settings.globalSettings();

    gs["io_button_bindings"] = nlohmann::json::object();
    for (int i = 0; i < BUTTON_COUNT; ++i) {
        if (buttons[i].isSet()) {
            gs["io_button_bindings"][djButtonNames[i]] = buttons[i].toJson();
        }
    }
    gs["dancer_button_bindings"] = nlohmann::json::object();
    for (int i = 0; i < DANCER_COUNT; ++i) {
        if (dancerButtons[i].isSet()) {
            gs["dancer_button_bindings"][dancerButtonNames[i]] = dancerButtons[i].toJson();
        }
    }

    gs["analog_bindings"] = nlohmann::json::object();
    for (int p = 0; p < ANALOG_COUNT; ++p) {
        if (analogs[p].isSet() || analogs[p].hasVtt()) {
            gs["analog_bindings"][analogPortKey(p)] = analogs[p].toJson();
        }
    }

    gs["light_bindings"] = nlohmann::json::object();
    for (int i = 0; i < LIGHT_COUNT; ++i) {
        if (lights[i].isSet()) {
            gs["light_bindings"][lightNames[i]] = lights[i].toJson();
        }
    }

    settings.save();
}

bool BindingStore::isHeld(const ButtonBinding& b) const {
    if (!b.isSet()) {
        return false;
    }
    if (b.isKeyboard()) {
        return (GetAsyncKeyState(b.vkCode) & 0x8000) != 0;
    }
    if (!mgr) {
        return false;
    }
    if (b.analogType != ButtonAnalogType::NONE) {
        float hatValue = mgr->getAxisValue(b.devicePath, b.buttonIdx);
        return isHatDirectionActive(hatValue, b.analogType);
    }
    return mgr->getButtonState(b.devicePath, b.buttonIdx);
}

std::string BindingStore::getDisplayString(const ButtonBinding& b) const {
    if (!b.isSet()) {
        return "(unbound)";
    }

    if (b.isKeyboard()) {
        return std::string("Key: ") + vkToName(b.vkCode);
    }

    if (!mgr) {
        return std::string("[Disconnected] ") + truncate(b.deviceName, 18);
    }

    std::vector<Device> devs = mgr->getDevices();
    for (const Device& dev : devs) {
        if (dev.path == b.devicePath) {
            std::string label;
            if (b.buttonIdx >= 0 && b.buttonIdx < static_cast<int>(dev.buttonCapsNames.size())) {
                label = dev.buttonCapsNames[b.buttonIdx];
            } else {
                label = std::string("Button ") + std::to_string(b.buttonIdx);
            }
            std::string devName = truncate(dev.name, 18);
            return label + " [" + devName + "]";
        }
    }
    // Device not found — disconnected fallback
    return std::string("[Disconnected] ") + truncate(b.deviceName, 18);
}

std::string BindingStore::getDisplayString(const AnalogBinding& a) const {
    if (!a.isSet()) {
        return "(unbound)";
    }

    if (a.hasMouse()) {
        const char* axisName = (a.mouseAxis == 0) ? "X" : "Y";
        std::string label = a.mouseName + " / " + axisName;
        if (mgr) {
            auto mice = mgr->getMouseDevices();
            bool found = false;
            for (auto& m : mice) {
                if (m.path == a.mousePath) { found = true; break; }
            }
            if (!found) return "[Disconnected] " + label;
        }
        return label;
    }

    if (!mgr) {
        return std::string("[Disconnected] ") + a.deviceName;
    }

    std::vector<Device> devs = mgr->getDevices();
    for (const Device& dev : devs) {
        if (dev.path == a.devicePath) {
            std::string axisLabel;
            if (a.axisIdx >= 0 && a.axisIdx < static_cast<int>(dev.valueCapsNames.size())) {
                axisLabel = dev.valueCapsNames[a.axisIdx];
            } else {
                axisLabel = std::string("Axis ") + std::to_string(a.axisIdx);
            }
            return dev.name + " / " + axisLabel;
        }
    }
    return std::string("[Disconnected] ") + a.deviceName;
}

std::string BindingStore::getDisplayString(const LightBinding& l) const {
    if (!l.isSet()) {
        return "(unbound)";
    }

    if (!mgr) {
        return std::string("[Disconnected] ") + l.deviceName;
    }

    std::vector<Device> devs = mgr->getDevices();
    for (const auto& dev : devs) {
        if (dev.path != l.devicePath) {
            continue;
        }
        int buttonCount = static_cast<int>(dev.buttonOutputCapsNames.size());
        if (l.outputIdx < buttonCount) {
            return dev.buttonOutputCapsNames[l.outputIdx] + " (" + dev.name + ")";
        }
        int valueIndex = l.outputIdx - buttonCount;
        if (valueIndex < static_cast<int>(dev.valueOutputCapsNames.size())) {
            return dev.valueOutputCapsNames[valueIndex] + " (" + dev.name + ")";
        }
        return "Output " + std::to_string(l.outputIdx) + " (" + dev.name + ")";
    }
    return "[Disconnected] " + l.deviceName;
}

uint8_t BindingStore::getAnalogPosition(const AnalogBinding& a, uint8_t vttPos, uint8_t mousePos) const {
    if (!a.isSet()) {
        return static_cast<uint8_t>(static_cast<int>(vttPos) + static_cast<int>(mousePos) - TT_CENTER);
    }
    int base;
    if (a.hasMouse()) {
        base = a.reverse ? (256 - static_cast<int>(mousePos)) : static_cast<int>(mousePos);
    } else {
        float raw = mgr ? mgr->getAxisValue(a.devicePath, a.axisIdx) : 0.5f;
        if (a.reverse) raw = 1.0f - raw;
        base = static_cast<int>(raw * 255.0f);
    }
    return static_cast<uint8_t>(base + static_cast<int>(vttPos) - TT_CENTER);
}

bool BindingStore::isHeldSnapshot(const ButtonBinding& b, const DeviceSnapshotMap& deviceSnapshots) const {
    if (!b.isSet()) {
        return false;
    }
    if (b.isKeyboard()) {
        return (GetAsyncKeyState(b.vkCode) & 0x8000) != 0;
    }
    auto it = deviceSnapshots.find(b.devicePath);
    if (it == deviceSnapshots.end()) {
        return false;
    }
    const DeviceSnapshot& ds = it->second;
    if (b.analogType != ButtonAnalogType::NONE) {
        if (b.buttonIdx < 0 || b.buttonIdx >= static_cast<int>(ds.values.size())) {
            return false;
        }
        return isHatDirectionActive(ds.values[b.buttonIdx], b.analogType);
    }
    if (b.buttonIdx < 0 || b.buttonIdx >= static_cast<int>(ds.buttons.size())) {
        return false;
    }
    return ds.buttons[b.buttonIdx];
}

uint8_t BindingStore::getPositionSnapshot(const AnalogBinding& a, uint8_t vttPos, uint8_t mousePos, const DeviceSnapshotMap& deviceSnapshots) const {
    if (!a.isSet()) {
        return static_cast<uint8_t>(static_cast<int>(vttPos) + static_cast<int>(mousePos) - TT_CENTER);
    }
    int base;
    if (a.hasMouse()) {
        base = a.reverse ? (256 - static_cast<int>(mousePos)) : static_cast<int>(mousePos);
    } else {
        float raw = 0.5f;
        auto it = deviceSnapshots.find(a.devicePath);
        if (it != deviceSnapshots.end() && a.axisIdx >= 0 && a.axisIdx < static_cast<int>(it->second.values.size())) {
            raw = it->second.values[a.axisIdx];
        }
        if (a.reverse) raw = 1.0f - raw;
        base = static_cast<int>(raw * 255.0f);
    }
    return static_cast<uint8_t>(base + static_cast<int>(vttPos) - TT_CENTER);
}
