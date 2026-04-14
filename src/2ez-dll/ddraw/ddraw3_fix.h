#pragma once

/**
 * DDraw Render Fixes for EZ2DJ 1st TraX SE
 *
 * Fixes rendering issues present on non-ForceWare era drivers on Windows XP.
 * 99% of these problems are not present on win10/11 when using ddraw compat with the
 * simple 1st SE compatiblilty patch ("Fix Windows XP+ Compatibility")
 * Wraps IDirect3DDevice3 and hooks BltFast/Blt on the backbuffer.
 *
 * - Backbuffer clear via opaque black quad each frame (prevents ghosting)
 * - Point texture filtering (optional, prevents bilinear blurring)
 * - BltFast/Blt source blits rerouted through D3D textured quads
 * - Blt COLORFILL rerouted through D3D colored quad
 * - D3D scene kept open during mixed DDraw/D3D rendering
 * - Transition hold: prevents black screen during scene transitions
 *
 * Supported games: ez2dj_1st_se
 * Fixes do not currently work for remember 1st, low prio as ghosting only occurs on a select few song BG's.
 * otherwise generally works fine.
 */

#include <string>

namespace DDraw3Fix {
    void install(const std::string& gameId, bool force32bpp, bool pointFiltering, bool texelAlignment);
}
