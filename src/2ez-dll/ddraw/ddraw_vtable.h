#pragma once

// Vtable slot indices for the DDraw/D3D COM interfaces we hook.
// Slot numbers are part of the COM ABI contract and follow the declaration
// order of each interface in <d3d.h> / <ddraw.h> — they will not change.

namespace VT {

namespace IDirectDraw {
    enum { QueryInterface = 0 };
}

namespace IDirectDraw4 {
    enum { QueryInterface = 0, SetDisplayMode = 21 };
}

namespace IDirectDraw7 {
    enum { QueryInterface = 0, SetDisplayMode = 21 };
}

namespace IDirect3D3 {
    enum { CreateDevice = 8 };
}

namespace IDirect3D7 {
    enum { CreateDevice = 4 };
}

namespace IDirect3DDevice3 {
    enum { DrawPrimitive = 28, DrawIndexedPrimitive = 29 };
}

namespace IDirect3DDevice7 {
    enum { DrawPrimitive = 25 };
}

namespace IDirectDrawSurface7 {
    enum { Blt = 5, BltFast = 7 };
}

} // namespace VT
