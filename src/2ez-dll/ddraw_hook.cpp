#include "ddraw_hook.h"
#include <windows.h>
#include <ddraw.h>

static bool s_force60hz = false;
using SetDisplayModeFn = HRESULT(WINAPI*)(void*, DWORD, DWORD, DWORD, DWORD, DWORD);
static SetDisplayModeFn s_origDD2 = nullptr;
static SetDisplayModeFn s_origDD4 = nullptr;
static SetDisplayModeFn s_origDD7 = nullptr;

static HRESULT setDisplayModeCommon(
    void* pThis, DWORD dwWidth, DWORD dwHeight, DWORD dwBPP,
    DWORD dwRefreshRate, DWORD dwFlags, SetDisplayModeFn orig)
{
    if (s_force60hz) {
        HRESULT hr = orig(pThis, dwWidth, dwHeight, dwBPP, 60, dwFlags);
        if (SUCCEEDED(hr)) return hr;
        // 60Hz rejected by driver — fall back to original rate
    }
    return orig(pThis, dwWidth, dwHeight, dwBPP, dwRefreshRate, dwFlags);
}

static HRESULT WINAPI HookedSetDisplayModeDD2(void* pThis, DWORD w, DWORD h, DWORD bpp, DWORD rate, DWORD flags) {
    return setDisplayModeCommon(pThis, w, h, bpp, rate, flags, s_origDD2);
}
static HRESULT WINAPI HookedSetDisplayModeDD4(void* pThis, DWORD w, DWORD h, DWORD bpp, DWORD rate, DWORD flags) {
    return setDisplayModeCommon(pThis, w, h, bpp, rate, flags, s_origDD4);
}
static HRESULT WINAPI HookedSetDisplayModeDD7(void* pThis, DWORD w, DWORD h, DWORD bpp, DWORD rate, DWORD flags) {
    return setDisplayModeCommon(pThis, w, h, bpp, rate, flags, s_origDD7);
}

static void hookSlot(void* pIface, int slot, void* hookFn, SetDisplayModeFn* origOut) {
    void** vtable = *reinterpret_cast<void***>(pIface);
    if (vtable[slot] == hookFn) return;
    *origOut = reinterpret_cast<SetDisplayModeFn>(vtable[slot]);
    DWORD oldProt;
    VirtualProtect(&vtable[slot], sizeof(void*), PAGE_READWRITE, &oldProt);
    vtable[slot] = hookFn;
    VirtualProtect(&vtable[slot], sizeof(void*), oldProt, &oldProt);
}

void installDDrawHook(bool force60hz) {
    s_force60hz = force60hz;

    HMODULE hDDraw = GetModuleHandleA("ddraw.dll");
    if (!hDDraw) hDDraw = LoadLibraryA("ddraw.dll");
    if (!hDDraw) return;

    // Use DirectDrawCreateEx (v7 API) to avoid corrupting global DDraw state
    // that DirectDrawCreate (v1 API) causes before the game's own CreateEx call.
    auto fnCreateEx = reinterpret_cast<HRESULT(WINAPI*)(GUID*, void**, REFIID, IUnknown*)>(
        GetProcAddress(hDDraw, "DirectDrawCreateEx"));
    if (!fnCreateEx) return;

    IDirectDraw7* pDD7 = nullptr;
    if (FAILED(fnCreateEx(nullptr, reinterpret_cast<void**>(&pDD7), IID_IDirectDraw7, nullptr))) return;

    hookSlot(pDD7, 21, reinterpret_cast<void*>(HookedSetDisplayModeDD7), &s_origDD7);

    // QI to DD4 and hook (games using DirectDrawCreateEx with IID_IDirectDraw4)
    IDirectDraw4* pDD4 = nullptr;
    if (SUCCEEDED(pDD7->QueryInterface(IID_IDirectDraw4, reinterpret_cast<void**>(&pDD4)))) {
        hookSlot(pDD4, 21, reinterpret_cast<void*>(HookedSetDisplayModeDD4), &s_origDD4);
        pDD4->Release();
    }

    // QI to DD2 and hook (older games using DirectDrawCreate)
    IDirectDraw2* pDD2 = nullptr;
    if (SUCCEEDED(pDD7->QueryInterface(IID_IDirectDraw2, reinterpret_cast<void**>(&pDD2)))) {
        hookSlot(pDD2, 21, reinterpret_cast<void*>(HookedSetDisplayModeDD2), &s_origDD2);
        pDD2->Release();
    }

    pDD7->Release();
}
