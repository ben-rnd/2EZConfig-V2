#pragma once
#include <string>
#include "game_defs.h"

class SettingsManager;

namespace SharedIO {

void earlyInit(SettingsManager* settings, std::string& gameId, GameFamily family);

}  // namespace SharedIO
