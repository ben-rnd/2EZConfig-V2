#pragma once

/**
 * DDraw7 Rendering Fixes for 2nd TraX and onwards (KEngine games)
 *
 * Hooks are discovered via vtable chaining from an inline detour on
 * ddraw.dll's DirectDrawCreateEx — no per-game addresses required.
 *
 * - Force 32-bit display mode (prevents crashes on Win10/11)
 * - Point texture filtering (optional, fixes various texture artifacts)
 * - Texel-to-pixel alignment (optional, corrects half-pixel offset on modern GPUs)
 *
 * Supported games: all EZ2DJ/EZ2Dancer titles using DirectDraw7 (2nd TraX onwards)
 */

#include <string>

namespace DDraw7Fix {
    void install(bool force32bpp, bool force60hz, bool pointFilter, bool texelAlignment);
}
