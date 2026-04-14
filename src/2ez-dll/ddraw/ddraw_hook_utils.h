#pragma once

/**
 * Shared primitives for DDraw/D3D vtable hooking.
 *
 * Stable math and patching helpers used by both ddraw3_fix and ddraw7_fix.
 * These do not depend on any specific D3D version.
 */

#include <windows.h>

namespace DDrawHookUtils {
    // Replace vtable[index] with hook, store the original into *origOut.
    // No-op if *origOut is already non-null (idempotent).
    void patchVtable(void** vtable, int index, void* hook, void** origOut);

    // Compute the byte stride of an FVF vertex (XYZRHW + diffuse/specular + N tex coords).
    DWORD computeFvfStride(DWORD fvf);

    // Returns true if all texture UVs in the vertex buffer are within [0,1] (with small tolerance).
    // Used to decide whether texture clamping is safe.
    bool uvsInUnitRange(void* verts, DWORD count, DWORD fvf);

    // Offset every vertex's X/Y position by (dx, dy). Used for texel-to-pixel alignment.
    void shiftVertexXY(void* verts, DWORD count, DWORD fvf, float dx, float dy);
}
