#pragma once

/**
 * DDraw7 Rendering Fixes for 2nd TraX and onwards (KEngine games)
 *
 * Hooks are discovered via vtable chaining from an inline detour on
 * ddraw.dll's DirectDrawCreateEx — no per-game addresses required.
 *
 * Reads settings from SettingsManager:
 *   force_32bit_display  (bool) — force 32-bit display mode
 *   force_60hz           (bool) — force 60Hz refresh rate
 *   point_filtering      (bool) — force POINT texture filtering
 *   texel_alignment      (bool) — apply -0.5px texel offset
 *
 * Supported games: all EZ2DJ/EZ2Dancer titles using DirectDraw7 (2nd TraX onwards)
 */

class SettingsManager;

namespace DDraw7Fix {
    void install(SettingsManager* settings);
}
