/**
 * DDraw7 Fixes/patches for 2nd TraX and onwards
 */

#include "ddraw7_fix.h"
#include "logger.h"

#include <windows.h>
#include <d3d.h>

#define D3DTSS_MAGFILTER_D7 ((D3DTEXTURESTAGESTATETYPE)16)
#define D3DTSS_MINFILTER_D7 ((D3DTEXTURESTAGESTATETYPE)17)
#define D3DTSS_MIPFILTER_D7 ((D3DTEXTURESTAGESTATETYPE)18)
#define D3DTFP_POINT 1
#define D3DTFP_NONE  0
#define D3DTSS_ADDRESSU_D7 ((D3DTEXTURESTAGESTATETYPE)12)
#define D3DTSS_ADDRESSV_D7 ((D3DTEXTURESTAGESTATETYPE)13)
#define D3DTADDRESS_CLAMP 3

// IID_IDirect3D7: {f5049e77-4861-11d2-a407-00a0c90629a8}
static const GUID IID_IDirect3D7_local =
    { 0xf5049e77, 0x4861, 0x11d2, { 0xa4, 0x07, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8 } };

// Types

typedef HRESULT (STDMETHODCALLTYPE *PFN_DrawPrimitive7)(IDirect3DDevice7* self, D3DPRIMITIVETYPE type, DWORD fvf, void* vertices, DWORD vertCount, DWORD flags);
typedef HRESULT (WINAPI *PFN_DirectDrawCreateEx)(GUID* lpGuid, LPVOID* lplpDD, REFIID iid, IUnknown* pUnkOuter);
typedef HRESULT (STDMETHODCALLTYPE *PFN_SetDisplayMode)(IDirectDraw7* self, DWORD width, DWORD height, DWORD bpp, DWORD refreshRate, DWORD flags);
typedef HRESULT (STDMETHODCALLTYPE *PFN_QueryInterface)(IUnknown* self, REFIID riid, void** ppv);
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateDevice)(IDirect3D7* self, REFCLSID rclsid, IDirectDrawSurface7* surface, IDirect3DDevice7** device);

// Static state

static bool s_force32bpp = false;
static bool s_pointFilter = false;
static bool s_texelAlignment = false;

static PFN_DirectDrawCreateEx g_origDirectDrawCreateEx = nullptr;
static PFN_SetDisplayMode g_origSetDisplayMode = nullptr;
static PFN_QueryInterface g_origDDraw7QueryInterface = nullptr;
static PFN_CreateDevice g_origCreateDevice = nullptr;
static PFN_DrawPrimitive7 g_origDrawPrimitive = nullptr;

// Vtable helper

static void PatchVtable(void** vtable, int index, void* hook, void** origOut) {
    if (*origOut) return;
    *origOut = vtable[index];
    DWORD oldProt;
    VirtualProtect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt);
    vtable[index] = hook;
    VirtualProtect(&vtable[index], sizeof(void*), oldProt, &oldProt);
}

// DrawPrimitive hook — point filtering + texel-to-pixel alignment

static HRESULT STDMETHODCALLTYPE Hooked_DrawPrimitive(IDirect3DDevice7* self, D3DPRIMITIVETYPE type, DWORD fvf, void* vertices, DWORD vertCount, DWORD flags) {
    DWORD savedMag = 0, savedMin = 0, savedMip = 0;
    DWORD savedAddrU = 0, savedAddrV = 0;
    bool didFilter = false;
    bool didAlign = false;
    bool didClamp = false;
    BYTE* v = (BYTE*)vertices;

    if (s_pointFilter) {
        self->GetTextureStageState(0, D3DTSS_MAGFILTER_D7, &savedMag);
        self->GetTextureStageState(0, D3DTSS_MINFILTER_D7, &savedMin);
        self->GetTextureStageState(0, D3DTSS_MIPFILTER_D7, &savedMip);
        self->SetTextureStageState(0, D3DTSS_MAGFILTER_D7, D3DTFP_POINT);
        self->SetTextureStageState(0, D3DTSS_MINFILTER_D7, D3DTFP_POINT);
        self->SetTextureStageState(0, D3DTSS_MIPFILTER_D7, D3DTFP_NONE);
        didFilter = true;
    }

    // Old D3D7 maps texels to pixels differently than modern GPUs expect.
    // This causes all sprites/textures to look slightly offset (by half a pixel).
    // We nudge every vertex back by 0.5px to compensate.
    // We also clamp texture addressing when safe, this prevents ugly texture wrapping (thin 1 pixel lines).
    if (s_texelAlignment && (fvf & D3DFVF_XYZRHW) && v && vertCount > 0) {
        DWORD stride = 16;
        if (fvf & D3DFVF_DIFFUSE)  stride += 4;
        if (fvf & D3DFVF_SPECULAR) stride += 4;
        stride += ((fvf >> 8) & 0xF) * 8;
        DWORD uvOffset = stride - ((fvf >> 8) & 0xF) * 8;

        // Check if UVs are within [0,1] — safe to clamp without breaking wrapping
        bool canClamp = true;
        for (DWORD i = 0; i < vertCount && canClamp; i++) {
            float u  = *(float*)(v + stride * i + uvOffset);
            float uv = *(float*)(v + stride * i + uvOffset + 4);
            if (u < -0.01f || u > 1.01f || uv < -0.01f || uv > 1.01f)
                canClamp = false;
        }

        if (canClamp) {
            self->GetTextureStageState(0, D3DTSS_ADDRESSU_D7, &savedAddrU);
            self->GetTextureStageState(0, D3DTSS_ADDRESSV_D7, &savedAddrV);
            self->SetTextureStageState(0, D3DTSS_ADDRESSU_D7, D3DTADDRESS_CLAMP);
            self->SetTextureStageState(0, D3DTSS_ADDRESSV_D7, D3DTADDRESS_CLAMP);
            didClamp = true;
        }

        for (DWORD i = 0; i < vertCount; i++) {
            *(float*)(v + stride * i)     -= 0.5f;
            *(float*)(v + stride * i + 4) -= 0.5f;
        }
        didAlign = true;
    }

    HRESULT hr = g_origDrawPrimitive(self, type, fvf, vertices, vertCount, flags);

    if (didAlign) {
        DWORD stride = 16;
        if (fvf & D3DFVF_DIFFUSE)  stride += 4;
        if (fvf & D3DFVF_SPECULAR) stride += 4;
        stride += ((fvf >> 8) & 0xF) * 8;

        for (DWORD i = 0; i < vertCount; i++) {
            *(float*)(v + stride * i)     += 0.5f;
            *(float*)(v + stride * i + 4) += 0.5f;
        }
        if (didClamp) {
            self->SetTextureStageState(0, D3DTSS_ADDRESSU_D7, savedAddrU);
            self->SetTextureStageState(0, D3DTSS_ADDRESSV_D7, savedAddrV);
        }
    }

    if (didFilter) {
        self->SetTextureStageState(0, D3DTSS_MAGFILTER_D7, savedMag);
        self->SetTextureStageState(0, D3DTSS_MINFILTER_D7, savedMin);
        self->SetTextureStageState(0, D3DTSS_MIPFILTER_D7, savedMip);
    }

    return hr;
}

// Vtable chain: CreateDevice -> hook DrawPrimitive

static HRESULT STDMETHODCALLTYPE Hooked_CreateDevice(IDirect3D7* self, REFCLSID rclsid, IDirectDrawSurface7* surface, IDirect3DDevice7** device) {
    HRESULT hr = g_origCreateDevice(self, rclsid, surface, device);
    if (SUCCEEDED(hr) && device && *device) {
        void** vtable = *(void***)*device;
        PatchVtable(vtable, 25, (void*)Hooked_DrawPrimitive, (void**)&g_origDrawPrimitive);
        Logger::info("[DDraw7Fix] Hooked IDirect3DDevice7::DrawPrimitive");

        if (s_pointFilter) {
            (*device)->SetTextureStageState(0, D3DTSS_MAGFILTER_D7, D3DTFP_POINT);
            (*device)->SetTextureStageState(0, D3DTSS_MINFILTER_D7, D3DTFP_POINT);
            Logger::info("[DDraw7Fix] Set initial POINT filtering");
        }
    }
    return hr;
}

// Vtable chain: QueryInterface -> hook IDirect3D7::CreateDevice

static HRESULT STDMETHODCALLTYPE Hooked_DDraw7QueryInterface(IUnknown* self, REFIID riid, void** ppv) {
    HRESULT hr = g_origDDraw7QueryInterface(self, riid, ppv);
    if (SUCCEEDED(hr) && ppv && *ppv && riid == IID_IDirect3D7_local) {
        void** vtable = *(void***)*ppv;
        PatchVtable(vtable, 4, (void*)Hooked_CreateDevice, (void**)&g_origCreateDevice);
        Logger::info("[DDraw7Fix] Hooked IDirect3D7::CreateDevice");
    }
    return hr;
}

// Vtable chain: SetDisplayMode (force 32-bit)

static HRESULT STDMETHODCALLTYPE Hooked_SetDisplayMode(IDirectDraw7* self, DWORD width, DWORD height, DWORD bpp, DWORD refreshRate, DWORD flags) {
    return g_origSetDisplayMode(self, width, height, 32, refreshRate, flags);
}

// IAT hook: DirectDrawCreateEx — entry point for all hooks

static HRESULT WINAPI Hooked_DirectDrawCreateEx(GUID* lpGuid, LPVOID* lplpDD, REFIID iid, IUnknown* pUnkOuter) {
    HRESULT hr = g_origDirectDrawCreateEx(lpGuid, lplpDD, iid, pUnkOuter);
    if (FAILED(hr) || !lplpDD || !*lplpDD) return hr;

    void** vtable = *(void***)*lplpDD;

    if (s_force32bpp)
        PatchVtable(vtable, 21, (void*)Hooked_SetDisplayMode, (void**)&g_origSetDisplayMode);

    if (s_pointFilter || s_texelAlignment)
        PatchVtable(vtable, 0, (void*)Hooked_DDraw7QueryInterface, (void**)&g_origDDraw7QueryInterface);

    return hr;
}

// Inline detour on ddraw.dll's DirectDrawCreateEx export

static bool s_hooked = false;
static BYTE s_trampoline[10] = {};

static bool TryHookInline() {
    if (s_hooked) return true;

    HMODULE ddraw = GetModuleHandleA("ddraw.dll");
    if (!ddraw) ddraw = GetModuleHandleA("DDRAW.dll");
    if (!ddraw) ddraw = GetModuleHandleA("DDRAW.DLL");
    if (!ddraw) return false;

    auto* target = (BYTE*)GetProcAddress(ddraw, "DirectDrawCreateEx");
    if (!target) return false;

    // Build trampoline: copy first 5 bytes, then jmp back to target+5
    DWORD oldProt;
    VirtualProtect(s_trampoline, sizeof(s_trampoline), PAGE_EXECUTE_READWRITE, &oldProt);
    memcpy(s_trampoline, target, 5);
    s_trampoline[5] = 0xE9;
    *(int32_t*)&s_trampoline[6] = (int32_t)((target + 5) - (s_trampoline + 10));

    g_origDirectDrawCreateEx = (PFN_DirectDrawCreateEx)s_trampoline;

    // Overwrite target with jmp to our hook
    VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &oldProt);
    target[0] = 0xE9;
    *(int32_t*)&target[1] = (int32_t)((BYTE*)Hooked_DirectDrawCreateEx - (target + 5));
    VirtualProtect(target, 5, oldProt, &oldProt);

    Logger::info("[DDraw7Fix] Hooked DirectDrawCreateEx in ddraw.dll");
    s_hooked = true;
    return true;
}

static DWORD WINAPI HookWatchThread(LPVOID) {
    for (int i = 0; i < 100 && !s_hooked; i++) {
        Sleep(50);
        TryHookInline();
    }
    if (!s_hooked)
        Logger::warn("[DDraw7Fix] Failed to hook DirectDrawCreateEx after retries");
    return s_hooked ? 0 : 1;
}

void DDraw7Fix::install(bool force32bpp, bool pointFilter, bool texelAlignment) {
    if (!force32bpp && !pointFilter && !texelAlignment) return;

    s_force32bpp = force32bpp;
    s_pointFilter = pointFilter;
    s_texelAlignment = texelAlignment;

    if (TryHookInline()) {
        Logger::info("[DDraw7Fix] Installed (32bpp=" + std::to_string(force32bpp) +
                     " pointFilter=" + std::to_string(pointFilter) +
                     " texelAlignment=" + std::to_string(texelAlignment) + ")");
        return;
    }

    // ddraw.dll not loaded yet — retry in background
    Logger::info("[DDraw7Fix] ddraw.dll not loaded yet, starting watch thread");
    CreateThread(nullptr, 0, HookWatchThread, nullptr, 0, nullptr);
}
