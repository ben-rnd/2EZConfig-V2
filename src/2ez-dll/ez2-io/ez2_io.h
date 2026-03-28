#pragma once
#include <string>

class BindingStore;
class InputManager;
class SettingsManager;

namespace EZ2IO {

// Called from DllMain before threads — hardlock init, remember1st resolution
void earlyInit(SettingsManager* settings, std::string& gameId);

// Called from InitThread — VEH registration, input polling, light flushing
void installHooks(BindingStore* bindings, InputManager* input,
                  SettingsManager* settings, const std::string& gameId);

}  // namespace EZ2IO
