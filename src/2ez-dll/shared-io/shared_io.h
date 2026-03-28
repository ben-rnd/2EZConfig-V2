#pragma once
#include <string>

class SettingsManager;

namespace SharedIO {

// Called from DllMain before threads — early init for game families that need it
// Currently: EZ2 hardlock init + remember1st resolution
void earlyInit(SettingsManager* settings, std::string& gameId);

}  // namespace SharedIO
