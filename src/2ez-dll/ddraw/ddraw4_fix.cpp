/**
 * D3D3 Fix for EZ2DJ 1st TraX SE
 *
 * Wraps IDirect3DDevice3 and hooks DDraw BltFast/Blt to fix rendering
 * on Windows XP.
 *
 * Fixes:
 * Fix 1: clearBackbuffer  — black quad at frame start prevents ghosting
 * Fix 2: BltFast hook     — DDraw bitmap draws → D3D textured quads
 * Fix 3: Scene management — keep D3D scene open for draws, close for DDraw
 * Fix 4: Blt KEYSRC hook  — meters that use Blt instead of BltFast
 * Fix 5: Blt COLORFILL    — fade-to-black via D3D quad
 * Fix 6: Transition hold  — preserve last EyeCatch animation frame
 */

#include "ddraw4_fix.h"
#include "ddraw_vertex_utils.h"
#include "ddraw_vtable.h"
#include "hooks.h"
#include "logger.h"

#include <windows.h>
#include <d3d.h>
#include <map>
#include <cstring>

// Types

struct Vertex2D { 
    float x, y, z, rhw;
    DWORD diffuse;
    DWORD specular;
    float u, v;
};

struct BmpCacheEntry {
    int width;
    int height;
    IDirectDrawSurface7* surface;
    char name[128];
};

struct TexEntry {
    float width;
    float height;
    HBITMAP hBitmap;
    IDirectDrawSurface7* surface;
    IDirect3DTexture2* texture;
    char* name;
};

struct TexCacheEntry { 
    IDirect3DTexture2* texture;
    char bmpName[128];
};

typedef int (__cdecl *PFN_CreateTextureSurface)(int entry, unsigned int w, unsigned int h, int flags);
typedef HRESULT (STDMETHODCALLTYPE *PFN_BltFast)(IDirectDrawSurface7*, DWORD, DWORD, IDirectDrawSurface7*, LPRECT, DWORD);
typedef HRESULT (STDMETHODCALLTYPE *PFN_Blt)(IDirectDrawSurface7*, LPRECT, IDirectDrawSurface7*, LPRECT, DWORD, LPDDBLTFX);

// Static variables

static uintptr_t s_deviceAddr = 0;
static uintptr_t s_backbufferAddr = 0;
static uintptr_t s_texCapsAddr = 0;
static uintptr_t s_createTexSurfaceFuncAddr = 0;
static uintptr_t s_bmpCacheAddr = 0;
static uintptr_t s_bmpCacheCountAddr = 0;

// Game-independent fix toggles (applied via vtable chain, work for both ez2dj_1st_se and rmbr_1st)
static bool s_force32bpp = false;
static bool s_force60hz = false;
static bool s_pointFiltering = false;
static bool s_texelAlignment = false;

// Hook chain: DirectDrawCreate -> IDirectDraw::QI -> IDirectDraw4::{SetDisplayMode, QI}
//                              -> IDirect3D3::CreateDevice -> IDirect3DDevice3::DrawPrimitive
typedef HRESULT (WINAPI *PFN_DirectDrawCreate)(GUID*, LPDIRECTDRAW*, IUnknown*);
typedef HRESULT (STDMETHODCALLTYPE *PFN_SetDisplayMode4)(void*, DWORD, DWORD, DWORD, DWORD, DWORD);
typedef HRESULT (STDMETHODCALLTYPE *PFN_QueryInterface)(IUnknown*, REFIID, void**);
typedef HRESULT (STDMETHODCALLTYPE *PFN_D3D3_CreateDevice)(void*, REFCLSID, void*, IDirect3DDevice3**, IUnknown*);
typedef HRESULT (STDMETHODCALLTYPE *PFN_D3D3_DrawPrimitive)(IDirect3DDevice3*, D3DPRIMITIVETYPE, DWORD, void*, DWORD, DWORD);
typedef HRESULT (STDMETHODCALLTYPE *PFN_D3D3_DrawIndexedPrimitive)(IDirect3DDevice3*, D3DPRIMITIVETYPE, DWORD, void*, DWORD, WORD*, DWORD, DWORD);

static PFN_DirectDrawCreate       g_origDirectDrawCreate       = nullptr;
static PFN_SetDisplayMode4        g_origSetDisplayMode         = nullptr;
// Single QI hook catches both IDirectDraw->IDirectDraw4 and IDirectDraw4->IDirect3D3
// because ddraw.dll shares the IUnknown::QueryInterface implementation across all
// its interfaces — MinHook's inline hook on the shared function fires for both.
static PFN_QueryInterface         g_origDDrawQueryInterface    = nullptr;
static PFN_D3D3_CreateDevice      g_origD3D3CreateDevice       = nullptr;
static PFN_D3D3_DrawPrimitive     g_origD3D3DrawPrimitive      = nullptr;
static PFN_D3D3_DrawIndexedPrimitive g_origD3D3DrawIndexedPrimitive = nullptr;
static bool s_32bppHooked = false;

// IIDs
// IID_IDirectDraw4: {9C59509A-39BD-11D1-8C4A-00C04FD930C5}
static const GUID IID_IDirectDraw4_local =
    { 0x9c59509a, 0x39bd, 0x11d1, { 0x8c, 0x4a, 0x00, 0xc0, 0x4f, 0xd9, 0x30, 0xc5 } };
// IID_IDirect3D3: {BB223240-E72B-11D0-A9B4-00AA00C0993E}
static const GUID IID_IDirect3D3_local =
    { 0xbb223240, 0xe72b, 0x11d0, { 0xa9, 0xb4, 0x00, 0xaa, 0x00, 0xc0, 0x99, 0x3e } };

// ---- SetDisplayMode hook (force 32bpp) ----
static HRESULT STDMETHODCALLTYPE Hooked_SetDisplayMode(void* self, DWORD w, DWORD h, DWORD bpp, DWORD refresh, DWORD flags) {
    DWORD newBpp     = s_force32bpp ? 32 : bpp;
    DWORD newRefresh = s_force60hz  ? 60 : refresh;
    Logger::info("[DDraw4Fix] SetDisplayMode " + std::to_string(w) + "x" + std::to_string(h) +
                 " bpp " + std::to_string(bpp) + "->" + std::to_string(newBpp) +
                 " refresh " + std::to_string(refresh) + "->" + std::to_string(newRefresh));
    return g_origSetDisplayMode(self, w, h, newBpp, newRefresh, flags);
}

// ---- DrawPrimitive hook (point filter + texel alignment) ----
// Sets POINT filter per-draw (setting once at CreateDevice isn't sticky —
// the game/driver resets it), and shifts vertex positions by -0.5px for
// texel alignment on modern GPUs.

// Saves up to 4 texture stage state values that we want to override for a draw,
// then restores them after. Only the entries we actually touch get saved/restored.
struct DrawStateSave {
    IDirect3DDevice3* device = nullptr;
    struct Entry { D3DTEXTURESTAGESTATETYPE state; DWORD savedValue; bool valid = false; };
    Entry magFilter, minFilter, addrU, addrV;

    void set(Entry& entry, D3DTEXTURESTAGESTATETYPE state, DWORD newValue) {
        entry.state = state;
        device->GetTextureStageState(0, state, &entry.savedValue);
        device->SetTextureStageState(0, state, newValue);
        entry.valid = true;
    }
    void restore(const Entry& entry) const {
        if (entry.valid) device->SetTextureStageState(0, entry.state, entry.savedValue);
    }
};

static bool applyPreDraw(IDirect3DDevice3* device, DWORD fvf, void* vertices, DWORD vertCount, DrawStateSave& saved) {
    saved.device = device;
    if (s_pointFiltering) {
        saved.set(saved.magFilter, D3DTSS_MAGFILTER, D3DTFG_POINT);
        saved.set(saved.minFilter, D3DTSS_MINFILTER, D3DTFN_POINT);
    }
    bool didAlign = false;
    if (s_texelAlignment && (fvf & D3DFVF_XYZRHW) && vertices && vertCount > 0) {
        if (DDrawVertexUtils::uvsInUnitRange(vertices, vertCount, fvf)) {
            saved.set(saved.addrU, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
            saved.set(saved.addrV, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
        }
        DDrawVertexUtils::shiftVertexXY(vertices, vertCount, fvf, -0.5f, -0.5f);
        didAlign = true;
    }
    return didAlign;
}

static void applyPostDraw(DWORD fvf, void* vertices, DWORD vertCount, bool didAlign, const DrawStateSave& saved) {
    if (didAlign)
        DDrawVertexUtils::shiftVertexXY(vertices, vertCount, fvf, +0.5f, +0.5f);
    saved.restore(saved.addrU);
    saved.restore(saved.addrV);
    saved.restore(saved.magFilter);
    saved.restore(saved.minFilter);
}

static HRESULT STDMETHODCALLTYPE Hooked_D3D3_DrawPrimitive(IDirect3DDevice3* device, D3DPRIMITIVETYPE type, DWORD fvf, void* vertices, DWORD vertCount, DWORD flags) {
    DrawStateSave saved;
    bool didAlign = applyPreDraw(device, fvf, vertices, vertCount, saved);
    HRESULT result = g_origD3D3DrawPrimitive(device, type, fvf, vertices, vertCount, flags);
    applyPostDraw(fvf, vertices, vertCount, didAlign, saved);
    return result;
}

static HRESULT STDMETHODCALLTYPE Hooked_D3D3_DrawIndexedPrimitive(IDirect3DDevice3* device, D3DPRIMITIVETYPE type, DWORD fvf, void* vertices, DWORD vertCount, WORD* indices, DWORD idxCount, DWORD flags) {
    DrawStateSave saved;
    bool didAlign = applyPreDraw(device, fvf, vertices, vertCount, saved);
    HRESULT result = g_origD3D3DrawIndexedPrimitive(device, type, fvf, vertices, vertCount, indices, idxCount, flags);
    applyPostDraw(fvf, vertices, vertCount, didAlign, saved);
    return result;
}

// ---- IDirect3D3::CreateDevice hook ----
// Hooks DrawPrimitive on the returned device to apply per-draw POINT filter
// and/or texel alignment.
static HRESULT STDMETHODCALLTYPE Hooked_D3D3_CreateDevice(void* self, REFCLSID rclsid, void* surface, IDirect3DDevice3** device, IUnknown* outer) {
    HRESULT hr = g_origD3D3CreateDevice(self, rclsid, surface, device, outer);
    if (SUCCEEDED(hr) && device && *device && !g_origD3D3DrawPrimitive) {
        void** vtable = *(void***)*device;
        if (hook_create(vtable[VT::IDirect3DDevice3::DrawPrimitive],        (void*)Hooked_D3D3_DrawPrimitive,        (void**)&g_origD3D3DrawPrimitive) &&
            hook_create(vtable[VT::IDirect3DDevice3::DrawIndexedPrimitive], (void*)Hooked_D3D3_DrawIndexedPrimitive, (void**)&g_origD3D3DrawIndexedPrimitive))
            Logger::info("[DDraw4Fix] Hooked IDirect3DDevice3::DrawPrimitive + DrawIndexedPrimitive");
    }
    return hr;
}

// ---- Unified QueryInterface hook ----
// Fires for IDirectDraw::QI, IDirectDraw4::QI (and any other DDraw interface
// sharing the IUnknown dispatcher). Dispatches by requested riid.
static HRESULT STDMETHODCALLTYPE Hooked_DDrawQueryInterface(IUnknown* self, REFIID riid, void** ppv) {
    HRESULT hr = g_origDDrawQueryInterface(self, riid, ppv);
    if (!SUCCEEDED(hr) || !ppv || !*ppv) return hr;

    if (riid == IID_IDirectDraw4_local && (s_force32bpp || s_force60hz) && !g_origSetDisplayMode) {
        void** vtable = *(void***)*ppv;
        if (hook_create(vtable[VT::IDirectDraw4::SetDisplayMode], (void*)Hooked_SetDisplayMode, (void**)&g_origSetDisplayMode))
            Logger::info("[DDraw4Fix] Hooked IDirectDraw4::SetDisplayMode");
    } else if (riid == IID_IDirect3D3_local && !g_origD3D3CreateDevice) {
        void** vtable = *(void***)*ppv;
        if (hook_create(vtable[VT::IDirect3D3::CreateDevice], (void*)Hooked_D3D3_CreateDevice, (void**)&g_origD3D3CreateDevice))
            Logger::info("[DDraw4Fix] Hooked IDirect3D3::CreateDevice");
    }
    return hr;
}

// ---- IAT entry hook ----
static HRESULT WINAPI Hooked_DirectDrawCreate(GUID* lpGuid, LPDIRECTDRAW* lplpDD, IUnknown* pUnkOuter) {
    HRESULT hr = g_origDirectDrawCreate(lpGuid, lplpDD, pUnkOuter);
    if (SUCCEEDED(hr) && lplpDD && *lplpDD && !g_origDDrawQueryInterface) {
        // ddraw.dll shares IUnknown::QueryInterface across all its interfaces
        // (IDirectDraw, IDirectDraw4, IDirect3D3, ...). Hooking once on the shared
        // function catches QIs from every interface, so we dispatch by riid inside
        // the hook rather than installing a separate hook per interface.
        void** vtable = *(void***)*lplpDD;
        if (hook_create(vtable[VT::IDirectDraw::QueryInterface], (void*)Hooked_DDrawQueryInterface, (void**)&g_origDDrawQueryInterface))
            Logger::info("[DDraw4Fix] Hooked shared DDraw QueryInterface");
    }
    return hr;
}

static bool TryHook32bpp() {
    if (s_32bppHooked) return true;
    if (!GetModuleHandleW(L"ddraw.dll")) return false;

    if (hook_create_api(L"ddraw.dll", "DirectDrawCreate",
                        (void*)Hooked_DirectDrawCreate,
                        (void**)&g_origDirectDrawCreate)) {
        Logger::info("[DDraw4Fix] Hooked DirectDrawCreate in ddraw.dll");
        s_32bppHooked = true;
        return true;
    }
    return false;
}

static DWORD WINAPI Hook32bppWatchThread(LPVOID) {
    for (int i = 0; i < 100 && !s_32bppHooked; i++) {
        Sleep(50);
        TryHook32bpp();
    }
    if (!s_32bppHooked)
        Logger::warn("[DDraw4Fix] Failed to hook DirectDrawCreate after retries");
    return s_32bppHooked ? 0 : 1;
}

static PFN_BltFast g_origBltFast = nullptr;
static PFN_Blt g_origBlt = nullptr;
static std::map<IDirectDrawSurface7*, TexCacheEntry> g_texCache;

// Internal helpers: save/restore render state + fullscreen quad drawing.
// Used by the device wrapper's clearBackbuffer (Fix 1), BltFast hook (Fix 2),
// and Blt COLORFILL hook (Fix 5) to avoid duplicating the same blend-state
// save/restore + quad draw dance everywhere.

struct SavedBlendState {
    IDirect3DTexture2* tex;
    DWORD src, dst, alphaEnable;
    DWORD colorKeyEnable;   // only used by BltFast hook
    bool hasColorKey;
};

static void saveBlendState(IDirect3DDevice3* dev, SavedBlendState& s, bool includeColorKey = false) {
    s.tex = nullptr;
    dev->GetTexture(0, &s.tex);
    dev->GetRenderState(D3DRENDERSTATE_SRCBLEND, &s.src);
    dev->GetRenderState(D3DRENDERSTATE_DESTBLEND, &s.dst);
    dev->GetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, &s.alphaEnable);
    s.hasColorKey = includeColorKey;
    if (includeColorKey)
        dev->GetRenderState(D3DRENDERSTATE_COLORKEYENABLE, &s.colorKeyEnable);
}

static void restoreBlendState(IDirect3DDevice3* dev, const SavedBlendState& s) {
    if (s.hasColorKey)
        dev->SetRenderState(D3DRENDERSTATE_COLORKEYENABLE, s.colorKeyEnable);
    dev->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, s.alphaEnable);
    dev->SetRenderState(D3DRENDERSTATE_SRCBLEND, s.src);
    dev->SetRenderState(D3DRENDERSTATE_DESTBLEND, s.dst);
    dev->SetTexture(0, s.tex);
    if (s.tex) s.tex->Release();
}

// Draw a fullscreen 640x480 colored quad with opaque blending.
// Used for backbuffer clear (color=0xFF000000) and Blt COLORFILL.
static void drawFullscreenQuad(IDirect3DDevice3* dev, DWORD argb) {
    Vertex2D quad[4] = {
        {   0.0f, 480.0f, 0.0f, 1.0f, argb, 0, 0.0f, 1.0f },
        {   0.0f,   0.0f, 0.0f, 1.0f, argb, 0, 0.0f, 0.0f },
        { 640.0f, 480.0f, 0.0f, 1.0f, argb, 0, 1.0f, 1.0f },
        { 640.0f,   0.0f, 0.0f, 1.0f, argb, 0, 1.0f, 0.0f },
    };
    dev->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ZERO);
    dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0x1C4, quad, 4, 0);
}

// IDirect3DDevice3 wrapper — Fix 1, Fix 3, Fix 6

class D3DDevice3Wrapper : public IDirect3DDevice3 {
public:
    IDirect3DDevice3* m_real;
    ULONG m_refCount;
    bool m_inScene;
    int m_transitionCountdown;
    int m_skipClearFrames;

    // Fix 1: Draw opaque black quad to clear the backbuffer.
    // Point filtering is set once at device creation via the vtable hook, no per-frame work needed.
    void clearBackbuffer() {
        SavedBlendState saved;
        saveBlendState(m_real, saved);
        m_real->SetTexture(0, nullptr);
        drawFullscreenQuad(m_real, 0xFF000000);
        restoreBlendState(m_real, saved);
    }

    // Fix 3: Scene management
    void ensureSceneActive() {
        if (!m_inScene) {
            m_real->BeginScene();
            m_inScene = true;
        }
    }

    void ensureSceneInactive() {
        if (m_inScene) {
            m_real->EndScene();
            m_inScene = false;
        }
    }

    // Fix 6: Trigger transition hold
    void onScreenCapture() {
        m_transitionCountdown = 26;
    }

    D3DDevice3Wrapper(IDirect3DDevice3* real)
        : m_real(real), m_refCount(1), m_inScene(false)
        , m_transitionCountdown(0), m_skipClearFrames(0) {}
    ~D3DDevice3Wrapper() {}

    // BeginScene — Fix 1: + Fix 6:
    // Skip Clear frames allows us to stop processing the back uffer fix so that effects 
    // that require not being cleared can work. This is essential for the transition after selecting a mode.
    HRESULT STDMETHODCALLTYPE BeginScene() override {
        HRESULT hr = m_real->BeginScene();
        if (SUCCEEDED(hr)) {
            m_inScene = true;
            if (m_transitionCountdown > 0) {
                --m_transitionCountdown;
                if (m_transitionCountdown == 0)
                    m_skipClearFrames = 60;
            }
            if (m_skipClearFrames > 0)
                --m_skipClearFrames;
            else
                clearBackbuffer();
        }
        return hr;
    }

    HRESULT STDMETHODCALLTYPE EndScene() override {
        m_inScene = false;
        return m_real->EndScene();
    }

    // DrawPrimitive — Fix 3:
    HRESULT STDMETHODCALLTYPE DrawPrimitive(
        D3DPRIMITIVETYPE t, DWORD f, void* v, DWORD c, DWORD fl) override
    {
        ensureSceneActive();
        return m_real->DrawPrimitive(t, f, v, c, fl);
    }

    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(
        D3DPRIMITIVETYPE pt, DWORD fvf, void* verts, DWORD vertCount,
        WORD* indices, DWORD idxCount, DWORD flags) override
    {
        ensureSceneActive();
        return m_real->DrawIndexedPrimitive(pt, fvf, verts, vertCount, indices, idxCount, flags);
    }

    // IUnknown — Release has ref counting logic, rest are passthrough
    ULONG STDMETHODCALLTYPE Release() override {
        m_real->Release();
        ULONG ref = --m_refCount;
        if (ref == 0) delete this;
        return ref;
    }

    // Pure passthrough
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override { return m_real->QueryInterface(riid, ppv); }
    ULONG STDMETHODCALLTYPE AddRef() override { m_real->AddRef(); return ++m_refCount; }
    HRESULT STDMETHODCALLTYPE GetCaps(D3DDEVICEDESC* a, D3DDEVICEDESC* b) override { return m_real->GetCaps(a, b); }
    HRESULT STDMETHODCALLTYPE GetStats(D3DSTATS* s) override { return m_real->GetStats(s); }
    HRESULT STDMETHODCALLTYPE AddViewport(IDirect3DViewport3* v) override { return m_real->AddViewport(v); }
    HRESULT STDMETHODCALLTYPE DeleteViewport(IDirect3DViewport3* v) override { return m_real->DeleteViewport(v); }
    HRESULT STDMETHODCALLTYPE NextViewport(IDirect3DViewport3* r, IDirect3DViewport3** v, DWORD f) override { return m_real->NextViewport(r, v, f); }
    HRESULT STDMETHODCALLTYPE EnumTextureFormats(LPD3DENUMPIXELFORMATSCALLBACK cb, void* ctx) override { return m_real->EnumTextureFormats(cb, ctx); }
    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D3** d) override { return m_real->GetDirect3D(d); }
    HRESULT STDMETHODCALLTYPE SetCurrentViewport(IDirect3DViewport3* v) override { return m_real->SetCurrentViewport(v); }
    HRESULT STDMETHODCALLTYPE GetCurrentViewport(IDirect3DViewport3** v) override { return m_real->GetCurrentViewport(v); }
    HRESULT STDMETHODCALLTYPE SetRenderTarget(IDirectDrawSurface4* s, DWORD f) override { return m_real->SetRenderTarget(s, f); }
    HRESULT STDMETHODCALLTYPE GetRenderTarget(IDirectDrawSurface4** s) override { return m_real->GetRenderTarget(s); }
    HRESULT STDMETHODCALLTYPE Begin(D3DPRIMITIVETYPE t, DWORD f, DWORD fl) override { return m_real->Begin(t, f, fl); }
    HRESULT STDMETHODCALLTYPE BeginIndexed(D3DPRIMITIVETYPE t, DWORD f, void* v, DWORD c, DWORD fl) override { return m_real->BeginIndexed(t, f, v, c, fl); }
    HRESULT STDMETHODCALLTYPE Vertex(void* v) override { return m_real->Vertex(v); }
    HRESULT STDMETHODCALLTYPE Index(WORD i) override { return m_real->Index(i); }
    HRESULT STDMETHODCALLTYPE End(DWORD f) override { return m_real->End(f); }
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE s, DWORD* v) override { return m_real->GetRenderState(s, v); }
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE s, DWORD v) override { return m_real->SetRenderState(s, v); }
    HRESULT STDMETHODCALLTYPE GetLightState(D3DLIGHTSTATETYPE s, DWORD* v) override { return m_real->GetLightState(s, v); }
    HRESULT STDMETHODCALLTYPE SetLightState(D3DLIGHTSTATETYPE s, DWORD v) override { return m_real->SetLightState(s, v); }
    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE s, D3DMATRIX* m) override { return m_real->SetTransform(s, m); }
    HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE s, D3DMATRIX* m) override { return m_real->GetTransform(s, m); }
    HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE s, D3DMATRIX* m) override { return m_real->MultiplyTransform(s, m); }
    HRESULT STDMETHODCALLTYPE SetClipStatus(D3DCLIPSTATUS* s) override { return m_real->SetClipStatus(s); }
    HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS* s) override { return m_real->GetClipStatus(s); }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveStrided(D3DPRIMITIVETYPE t, DWORD f, D3DDRAWPRIMITIVESTRIDEDDATA* d, DWORD c, DWORD fl) override { return m_real->DrawPrimitiveStrided(t, f, d, c, fl); }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveStrided(D3DPRIMITIVETYPE t, DWORD f, D3DDRAWPRIMITIVESTRIDEDDATA* d, DWORD vc, WORD* i, DWORD ic, DWORD fl) override { return m_real->DrawIndexedPrimitiveStrided(t, f, d, vc, i, ic, fl); }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveVB(D3DPRIMITIVETYPE t, IDirect3DVertexBuffer* vb, DWORD s, DWORD c, DWORD f) override { return m_real->DrawPrimitiveVB(t, vb, s, c, f); }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveVB(D3DPRIMITIVETYPE t, IDirect3DVertexBuffer* vb, WORD* i, DWORD c, DWORD f) override { return m_real->DrawIndexedPrimitiveVB(t, vb, i, c, f); }
    HRESULT STDMETHODCALLTYPE ComputeSphereVisibility(D3DVECTOR* cen, D3DVALUE* rad, DWORD c, DWORD f, DWORD* r) override { return m_real->ComputeSphereVisibility(cen, rad, c, f, r); }
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD stage, IDirect3DTexture2** tex) override { return m_real->GetTexture(stage, tex); }
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage, IDirect3DTexture2* tex) override { return m_real->SetTexture(stage, tex); }
    HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE t, DWORD* v) override { return m_real->GetTextureStageState(stage, t, v); }
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE t, DWORD v) override { return m_real->SetTextureStageState(stage, t, v); }
    HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD* p) override { return m_real->ValidateDevice(p); }
};

static D3DDevice3Wrapper* g_deviceWrapper = nullptr;

// Helpers — texture cache for Fix 2:

static unsigned int nextPow2(unsigned int v) {
    v--;
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return v + 1;
}

static const char* FindBitmapName(IDirectDrawSurface7* surface) {
    if (!s_bmpCacheAddr || !s_bmpCacheCountAddr) return nullptr;
    int count = *(int*)s_bmpCacheCountAddr;
    for (int i = 0; i < count; i++) {
        auto* entry = (BmpCacheEntry*)(s_bmpCacheAddr + 140 * i);
        if (entry->surface == surface)
            return entry->name;
    }
    return nullptr;
}

static IDirect3DTexture2* GetTextureForSurface(IDirectDrawSurface7* surface) {
    const char* curName = FindBitmapName(surface);

    auto it = g_texCache.find(surface);
    if (it != g_texCache.end()) {
        TexCacheEntry& cached = it->second;
        if (curName && cached.bmpName[0] && strcmp(curName, cached.bmpName) == 0)
            return cached.texture;
        g_texCache.erase(it);
    }

    if (!s_createTexSurfaceFuncAddr) return nullptr;
    if (!curName) return nullptr;

    Logger::info("[DDraw4Fix] BltFast: converting '" + std::string(curName) + "'");

    DDSURFACEDESC2 srcDesc = {};
    srcDesc.dwSize = sizeof(srcDesc);
    surface->GetSurfaceDesc(&srcDesc);
    int w = srcDesc.dwWidth;
    int h = srcDesc.dwHeight;

    HDC surfDC = nullptr;
    if (FAILED(surface->GetDC(&surfDC))) return nullptr;

    HDC memDC = CreateCompatibleDC(surfDC);
    HBITMAP hBmp = CreateCompatibleBitmap(surfDC, w, h);
    HGDIOBJ oldBmp = SelectObject(memDC, hBmp);
    BitBlt(memDC, 0, 0, w, h, surfDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteDC(memDC);
    surface->ReleaseDC(surfDC);

    auto* entry = (TexEntry*)calloc(1, sizeof(TexEntry));
    entry->hBitmap = hBmp;
    auto createTex = (PFN_CreateTextureSurface)s_createTexSurfaceFuncAddr;
    createTex((int)entry, w, h, 1);
    DeleteObject(hBmp);
    entry->hBitmap = nullptr;

    IDirect3DTexture2* tex = entry->texture;
    free(entry);

    if (tex) {
        TexCacheEntry cached = {};
        cached.texture = tex;
        strncpy(cached.bmpName, curName, sizeof(cached.bmpName) - 1);
        g_texCache[surface] = cached;
    }
    return tex;
}

// Fix 2: BltFast hook — DDraw bitmap draws → D3D textured quads
// Also contains Fix 6: screen capture detection for transition hold.

static HRESULT STDMETHODCALLTYPE Hooked_BltFast(
    IDirectDrawSurface7* self, DWORD dwX, DWORD dwY,
    IDirectDrawSurface7* lpSrc, LPRECT lpSrcRect, DWORD dwTrans)
{
    if (!g_deviceWrapper || !lpSrc)
        return g_origBltFast(self, dwX, dwY, lpSrc, lpSrcRect, dwTrans);

    IDirect3DTexture2* tex = GetTextureForSurface(lpSrc);
    if (!tex) {
        // Fix 6: Detect screen capture (fullscreen BltFast at 0,0 with no rect)
        if (dwX == 0 && dwY == 0 && !lpSrcRect)
            g_deviceWrapper->onScreenCapture();
        g_deviceWrapper->ensureSceneInactive();
        return g_origBltFast(self, dwX, dwY, lpSrc, lpSrcRect, dwTrans);
    }

    // Calculate UVs accounting for POW2/SQUAREONLY texture padding
    DDSURFACEDESC2 desc = {};
    desc.dwSize = sizeof(desc);
    lpSrc->GetSurfaceDesc(&desc);

    float srcW = (float)desc.dwWidth;
    float srcH = (float)desc.dwHeight;
    DWORD texCaps = s_texCapsAddr ? *(DWORD*)s_texCapsAddr : 0;
    float texW = srcW, texH = srcH;
    if (texCaps & 2)    { texW = (float)nextPow2((unsigned)srcW); texH = (float)nextPow2((unsigned)srcH); }
    if (texCaps & 0x20) { if (texW > texH) texH = texW; else texW = texH; }

    float u0 = 0.0f, v0 = 0.0f;
    float u1 = srcW / texW, v1 = srcH / texH;
    float dstW = srcW, dstH = srcH;
    if (lpSrcRect) {
        u0 = (float)lpSrcRect->left / texW;  v0 = (float)lpSrcRect->top / texH;
        u1 = (float)lpSrcRect->right / texW; v1 = (float)lpSrcRect->bottom / texH;
        dstW = (float)(lpSrcRect->right - lpSrcRect->left);
        dstH = (float)(lpSrcRect->bottom - lpSrcRect->top);
    }

    float x0 = (float)dwX, y0 = (float)dwY;
    float x1 = x0 + dstW,  y1 = y0 + dstH;

    Vertex2D quad[4] = {
        { x0, y1, 0.0f, 1.0f, 0xFFFFFFFF, 0, u0, v1 },
        { x0, y0, 0.0f, 1.0f, 0xFFFFFFFF, 0, u0, v0 },
        { x1, y1, 0.0f, 1.0f, 0xFFFFFFFF, 0, u1, v1 },
        { x1, y0, 0.0f, 1.0f, 0xFFFFFFFF, 0, u1, v0 },
    };

    IDirect3DDevice3* dev = g_deviceWrapper->m_real;
    SavedBlendState saved;
    saveBlendState(dev, saved, /*includeColorKey*/ true);

    g_deviceWrapper->ensureSceneActive();
    dev->SetTexture(0, tex);
    if (dwTrans & DDBLTFAST_SRCCOLORKEY)
        dev->SetRenderState(D3DRENDERSTATE_COLORKEYENABLE, TRUE);
    dev->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ZERO);
    dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0x1C4, quad, 4, 0);

    restoreBlendState(dev, saved);
    return DD_OK;
}

// Fix 4: Blt KEYSRC hook + Fix 5: Blt COLORFILL hook

static HRESULT STDMETHODCALLTYPE Hooked_Blt(
    IDirectDrawSurface7* self, LPRECT lpDestRect,
    IDirectDrawSurface7* lpSrc, LPRECT lpSrcRect,
    DWORD dwFlags, LPDDBLTFX lpFx)
{
    // Fix 5: COLORFILL → D3D quad
    if ((dwFlags & DDBLT_COLORFILL) && g_deviceWrapper && lpFx) {
        DWORD fillColor = lpFx->dwFillColor;
        DWORD argb = (fillColor == 0) ? 0xFF000000 : (0xFF000000 | fillColor);

        IDirect3DDevice3* dev = g_deviceWrapper->m_real;
        g_deviceWrapper->ensureSceneActive();

        SavedBlendState saved;
        saveBlendState(dev, saved);
        dev->SetTexture(0, nullptr);
        drawFullscreenQuad(dev, argb);
        restoreBlendState(dev, saved);
        return DD_OK;
    }

    // Fix 4: KEYSRC Blt → route through BltFast hook
    if ((dwFlags & DDBLT_KEYSRC) && g_deviceWrapper && lpSrc) {
        DWORD x = lpDestRect ? lpDestRect->left : 0;
        DWORD y = lpDestRect ? lpDestRect->top : 0;
        return Hooked_BltFast(self, x, y, lpSrc, lpSrcRect, DDBLTFAST_SRCCOLORKEY);
    }

    // All other Blt → pass through to DDraw
    if (g_deviceWrapper)
        g_deviceWrapper->ensureSceneInactive();
    return g_origBlt(self, lpDestRect, lpSrc, lpSrcRect, dwFlags, lpFx);
}

// Setup

static void InstallBltFastHook() {
    if (!s_backbufferAddr || !s_createTexSurfaceFuncAddr) return;

    IDirectDrawSurface7* backbuf = *(IDirectDrawSurface7**)s_backbufferAddr;
    if (!backbuf) return;

    void** vtable = *(void***)backbuf;

    if (!g_origBltFast)
        hook_create(vtable[VT::IDirectDrawSurface7::BltFast], (void*)Hooked_BltFast, (void**)&g_origBltFast);
    if (!g_origBlt)
        hook_create(vtable[VT::IDirectDrawSurface7::Blt], (void*)Hooked_Blt, (void**)&g_origBlt);

    Logger::info("[DDraw4Fix] Hooked backbuffer BltFast and Blt");
}

static DWORD WINAPI DDraw4FixThread(LPVOID) {
    Logger::info("[DDraw4Fix] Waiting for D3D device...");
    IDirect3DDevice3** devicePtr = reinterpret_cast<IDirect3DDevice3**>(s_deviceAddr);

    for (int i = 0; i < 300; ++i) {
        Sleep(100);
        if (*devicePtr != nullptr) {
            IDirect3DDevice3* realDevice = *devicePtr;
            auto* wrapper = new D3DDevice3Wrapper(realDevice);
            g_deviceWrapper = wrapper;
            *devicePtr = static_cast<IDirect3DDevice3*>(wrapper);
            Logger::info("[DDraw4Fix] Wrapped IDirect3DDevice3 (real=" +
                std::to_string(reinterpret_cast<uintptr_t>(realDevice)) + ")");
            Sleep(500);
            InstallBltFastHook();
            return 0;
        }
    }

    Logger::warn("[DDraw4Fix] Timed out waiting for D3D device");
    return 1;
}

// Public API

void DDraw4Fix::install(const std::string& gameId, bool ddraw4Fix, bool force32bpp, bool force60hz, bool pointFiltering, bool texelAlignment) {
    // Game-independent hook chain: installs DirectDrawCreate detour if any of the
    // fixes is enabled. Works for both ez2dj_1st_se and rmbr_1st.
    s_force32bpp     = force32bpp;
    s_force60hz      = force60hz;
    s_pointFiltering = pointFiltering;
    s_texelAlignment = texelAlignment;

    if (force32bpp || force60hz || pointFiltering || texelAlignment) {
        if (TryHook32bpp()) {
            Logger::info("[DDraw4Fix] Hook chain installed (32bpp=" + std::to_string(force32bpp) +
                         " 60hz=" + std::to_string(force60hz) +
                         " pointFilter=" + std::to_string(pointFiltering) +
                         " texelAlign=" + std::to_string(texelAlignment) + ")");
        } else {
            Logger::info("[DDraw4Fix] ddraw.dll not ready, starting retry thread");
            CreateThread(nullptr, 0, Hook32bppWatchThread, nullptr, 0, nullptr);
        }
    }

    // 1st SE specific: device wrapper + BltFast/Blt hooks require per-game addresses
    if (gameId == "ez2dj_1st_se" && ddraw4Fix) {
        s_deviceAddr             = 0x1EB7CC0;
        s_backbufferAddr         = 0x1EB7D08;
        s_texCapsAddr            = 0x1462880;
        s_createTexSurfaceFuncAddr = 0x422760;
        s_bmpCacheAddr           = 0xBAE268;
        s_bmpCacheCountAddr      = 0xBB6E68;
        Logger::info("[DDraw4Fix] Installing device wrapper for " + gameId);
        CreateThread(nullptr, 0, DDraw4FixThread, nullptr, 0, nullptr);
    } else {
        Logger::info("[DDraw4Fix] Device wrapper not installed for " + gameId);
    }
}
