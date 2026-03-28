#pragma once
#include <string>

class BindingStore;
class InputManager;
class SettingsManager;

namespace EZ2DancerIO {

void installHooks(SettingsManager* settings);
void initialiseIO(BindingStore* bindings, InputManager* input,
                   SettingsManager* settings);

}  // namespace EZ2DancerIO
