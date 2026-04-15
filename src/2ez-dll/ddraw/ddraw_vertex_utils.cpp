#include "ddraw_vertex_utils.h"

#include <d3d.h>

namespace DDrawVertexUtils {

DWORD computeFvfStride(DWORD fvf) {
    DWORD stride = 16;                        // XYZRHW (4 floats)
    if (fvf & D3DFVF_DIFFUSE)  stride += 4;
    if (fvf & D3DFVF_SPECULAR) stride += 4;
    stride += ((fvf >> 8) & 0xF) * 8;         // N texcoords (2 floats each)
    return stride;
}

bool uvsInUnitRange(void* verts, DWORD count, DWORD fvf) {
    BYTE* v = (BYTE*)verts;
    DWORD stride   = computeFvfStride(fvf);
    DWORD uvOffset = stride - ((fvf >> 8) & 0xF) * 8;
    for (DWORD i = 0; i < count; i++) {
        float u = *(float*)(v + stride * i + uvOffset);
        float t = *(float*)(v + stride * i + uvOffset + 4);
        if (u < -0.01f || u > 1.01f || t < -0.01f || t > 1.01f)
            return false;
    }
    return true;
}

void shiftVertexXY(void* verts, DWORD count, DWORD fvf, float dx, float dy) {
    BYTE* v = (BYTE*)verts;
    DWORD stride = computeFvfStride(fvf);
    for (DWORD i = 0; i < count; i++) {
        *(float*)(v + stride * i)     += dx;
        *(float*)(v + stride * i + 4) += dy;
    }
}

} // namespace DDrawVertexUtils
