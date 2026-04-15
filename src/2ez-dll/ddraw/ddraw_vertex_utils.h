#pragma once

#include <windows.h>

// FVF-stride / UV / vertex helpers used by both ddraw3_fix and ddraw7_fix.
// Not hook code — pure math.
namespace DDrawVertexUtils {
    DWORD computeFvfStride(DWORD fvf);
    bool  uvsInUnitRange(void* verts, DWORD count, DWORD fvf);
    void  shiftVertexXY(void* verts, DWORD count, DWORD fvf, float dx, float dy);
}
