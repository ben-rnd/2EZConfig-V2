#pragma once

#include <string>

// Hotkey-driven autoplay toggle. Mirrors 2EZConfig V1 behavior:
// tapping the configured key flips a single byte at a game-specific
// offset, toggling the game's internal autoplay flag.
//
// Supported game IDs (others are no-ops):
//   ez2ac_fn      — Final
//   ez2ac_fn_ex   — Final:EX
//   ez2ac_nt      — Night Traveller
//   ez2ac_ec      — Endless Circulation
namespace Autoplay {

void init(const std::string& gameId);

}  // namespace Autoplay
