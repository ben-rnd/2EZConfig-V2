#pragma once

/**
 * DDraw4 Render Fixes for EZ2DJ 1st TraX SE / Remember 1st
 *
 * Hooks DirectDrawCreate -> IDirectDraw4 -> IDirect3D3 -> IDirect3DDevice3
 * to apply display-mode overrides (32bpp, 60Hz), point filtering, and
 * texel alignment. For 1st SE, also wraps the D3D3 device and hooks
 * BltFast/Blt on the backbuffer to fix rendering on Windows XP.
 *
 * Reads settings from SettingsManager:
 *   ddraw4_fix              (bool) — enable device wrapper (1st SE only)
 *   ddraw4_force_32bpp      (bool) — force 32-bit display mode
 *   ddraw4_force_60hz       (bool) — force 60Hz refresh rate
 *   ddraw4_point_filtering  (bool) — force POINT texture filtering
 *   ddraw4_texel_alignment  (bool) — apply -0.5px texel offset
 */

#include <string>

class SettingsManager;

namespace DDraw4Fix {
    void install(const std::string& gameId, SettingsManager* settings);
}
