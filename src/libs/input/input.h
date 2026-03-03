#pragma once

#include "settings.h"
#include <hidapi.h>
#include <string>
#include <cstdint>

namespace Input {

// Returns true if the button bound to gameAction is currently pressed.
// Looks up the device and button index from settings, opens the HID device,
// reads one input report, and checks the relevant bit.
bool getButtonState(SettingsManager& settings, const std::string& gameAction);

} // namespace Input
