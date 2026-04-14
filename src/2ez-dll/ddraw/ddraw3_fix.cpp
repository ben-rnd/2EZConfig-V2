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

#include "ddraw3_fix.h"
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

static bool s_pointFiltering = false;

static PFN_BltFast g_origBltFast = nullptr;
static PFN_Blt g_origBlt = nullptr;
static std::map<IDirectDrawSurface7*, TexCacheEntry> g_texCache;

// IDirect3DDevice3 wrapper — Fix 1, Fix 3, Fix 6

class D3DDevice3Wrapper : public IDirect3DDevice3 {
public:
    IDirect3DDevice3* m_real;
    ULONG m_refCount;
    bool m_inScene;
    int m_transitionCountdown;
    int m_skipClearFrames;

    // Fix 1: Draw opaque black quad to clear the backbuffer
    void clearBackbuffer() {
        IDirect3DTexture2* savedTex = nullptr;
        m_real->GetTexture(0, &savedTex);
        m_real->SetTexture(0, nullptr);

        DWORD savedSrc, savedDst, savedAlpha;
        m_real->GetRenderState(D3DRENDERSTATE_SRCBLEND, &savedSrc);
        m_real->GetRenderState(D3DRENDERSTATE_DESTBLEND, &savedDst);
        m_real->GetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, &savedAlpha);

        m_real->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, FALSE);
        m_real->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ONE);
        m_real->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ZERO);

        Vertex2D quad[4] = {
            {   0.0f, 480.0f, 0.0f, 1.0f, 0xFF000000, 0, 0.0f, 1.0f },
            {   0.0f,   0.0f, 0.0f, 1.0f, 0xFF000000, 0, 0.0f, 0.0f },
            { 640.0f, 480.0f, 0.0f, 1.0f, 0xFF000000, 0, 1.0f, 1.0f },
            { 640.0f,   0.0f, 0.0f, 1.0f, 0xFF000000, 0, 1.0f, 0.0f },
        };
        m_real->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0x1C4, quad, 4, 0);

        m_real->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, savedAlpha);
        m_real->SetRenderState(D3DRENDERSTATE_SRCBLEND, savedSrc);
        m_real->SetRenderState(D3DRENDERSTATE_DESTBLEND, savedDst);
        m_real->SetTexture(0, savedTex);
        if (savedTex) savedTex->Release();

        if (s_pointFiltering) {
            m_real->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTFG_POINT);
            m_real->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTFN_POINT);
        }
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

    Logger::info("[D3D3Fix] BltFast: converting '" + std::string(curName) + "'");

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

    IDirect3DTexture2* savedTex = nullptr;
    DWORD savedSrc, savedDst, savedAlpha, savedColorKey;
    dev->GetTexture(0, &savedTex);
    dev->GetRenderState(D3DRENDERSTATE_SRCBLEND, &savedSrc);
    dev->GetRenderState(D3DRENDERSTATE_DESTBLEND, &savedDst);
    dev->GetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, &savedAlpha);
    dev->GetRenderState(D3DRENDERSTATE_COLORKEYENABLE, &savedColorKey);

    g_deviceWrapper->ensureSceneActive();
    dev->SetTexture(0, tex);
    if (dwTrans & DDBLTFAST_SRCCOLORKEY)
        dev->SetRenderState(D3DRENDERSTATE_COLORKEYENABLE, TRUE);
    dev->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ZERO);
    dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0x1C4, quad, 4, 0);

    dev->SetRenderState(D3DRENDERSTATE_COLORKEYENABLE, savedColorKey);
    dev->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, savedAlpha);
    dev->SetRenderState(D3DRENDERSTATE_SRCBLEND, savedSrc);
    dev->SetRenderState(D3DRENDERSTATE_DESTBLEND, savedDst);
    dev->SetTexture(0, savedTex);
    if (savedTex) savedTex->Release();

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

        Vertex2D quad[4] = {
            {   0.0f, 480.0f, 0.0f, 1.0f, argb, 0, 0.0f, 1.0f },
            {   0.0f,   0.0f, 0.0f, 1.0f, argb, 0, 0.0f, 0.0f },
            { 640.0f, 480.0f, 0.0f, 1.0f, argb, 0, 1.0f, 1.0f },
            { 640.0f,   0.0f, 0.0f, 1.0f, argb, 0, 1.0f, 0.0f },
        };

        IDirect3DDevice3* dev = g_deviceWrapper->m_real;
        g_deviceWrapper->ensureSceneActive();

        IDirect3DTexture2* savedTex = nullptr;
        DWORD savedSrc, savedDst, savedAlpha;
        dev->GetTexture(0, &savedTex);
        dev->GetRenderState(D3DRENDERSTATE_SRCBLEND, &savedSrc);
        dev->GetRenderState(D3DRENDERSTATE_DESTBLEND, &savedDst);
        dev->GetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, &savedAlpha);

        dev->SetTexture(0, nullptr);
        dev->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, FALSE);
        dev->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ONE);
        dev->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ZERO);
        dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0x1C4, quad, 4, 0);

        dev->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, savedAlpha);
        dev->SetRenderState(D3DRENDERSTATE_SRCBLEND, savedSrc);
        dev->SetRenderState(D3DRENDERSTATE_DESTBLEND, savedDst);
        dev->SetTexture(0, savedTex);
        if (savedTex) savedTex->Release();

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
    DWORD oldProt;

    g_origBltFast = (PFN_BltFast)vtable[7];
    VirtualProtect(&vtable[7], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt);
    vtable[7] = (void*)Hooked_BltFast;
    VirtualProtect(&vtable[7], sizeof(void*), oldProt, &oldProt);

    g_origBlt = (PFN_Blt)vtable[5];
    VirtualProtect(&vtable[5], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt);
    vtable[5] = (void*)Hooked_Blt;
    VirtualProtect(&vtable[5], sizeof(void*), oldProt, &oldProt);

    Logger::info("[D3D3Fix] Hooked backbuffer BltFast and Blt");
}

static DWORD WINAPI D3D3FixThread(LPVOID) {
    Logger::info("[D3D3Fix] Waiting for D3D device...");
    IDirect3DDevice3** devicePtr = reinterpret_cast<IDirect3DDevice3**>(s_deviceAddr);

    for (int i = 0; i < 300; ++i) {
        Sleep(100);
        if (*devicePtr != nullptr) {
            IDirect3DDevice3* realDevice = *devicePtr;
            auto* wrapper = new D3DDevice3Wrapper(realDevice);
            g_deviceWrapper = wrapper;
            *devicePtr = static_cast<IDirect3DDevice3*>(wrapper);
            Logger::info("[D3D3Fix] Wrapped IDirect3DDevice3 (real=" +
                std::to_string(reinterpret_cast<uintptr_t>(realDevice)) + ")");
            Sleep(500);
            InstallBltFastHook();
            return 0;
        }
    }

    Logger::warn("[D3D3Fix] Timed out waiting for D3D device");
    return 1;
}

// Public API

void DDraw3Fix::install(const std::string& gameId, bool pointFiltering) {
    if (gameId == "ez2dj_1st_se") {
        s_deviceAddr             = 0x1EB7CC0;
        s_backbufferAddr         = 0x1EB7D08;
        s_texCapsAddr            = 0x1462880;
        s_createTexSurfaceFuncAddr = 0x422760;
        s_bmpCacheAddr           = 0xBAE268;
        s_bmpCacheCountAddr      = 0xBB6E68;
    } else {
        return;
    }

    s_pointFiltering = pointFiltering;
    Logger::info("[D3D3Fix] Installing for " + gameId);
    CreateThread(nullptr, 0, D3D3FixThread, nullptr, 0, nullptr);
}
