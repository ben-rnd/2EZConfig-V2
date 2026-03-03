#include "input.h"
#include "binding.h"
#include "device_info.h"

void DeviceInfo::close() {}
void Input::init(SettingsManager&) {}
void Input::shutdown() {}
std::vector<Input::DeviceDesc> Input::enumerateDevices() { return {}; }
bool Input::getButtonState(const std::string&) { return false; }
uint8_t Input::getAnalogValue(const std::string&) { return 128; }
