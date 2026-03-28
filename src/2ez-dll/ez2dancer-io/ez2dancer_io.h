#pragma once
#include <string>

class BindingStore;
class InputManager;
class SettingsManager;

namespace EZ2DancerIO {

void installHooks(BindingStore* bindings, InputManager* input,
                  SettingsManager* settings);

}  // namespace EZ2DancerIO
