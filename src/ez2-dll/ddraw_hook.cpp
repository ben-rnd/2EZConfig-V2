#include "ddraw_hook.h"
#include <windows.h>
#include <ddraw.h>

static bool s_force60hz = false;
using SetDisplayModeFn = HRESULT(WINAPI*)(void*, DWORD, DWORD, DWORD, DWORD, DWORD);
static SetDisplayModeFn s_origSetDisplayMode = nullptr;

static HRESULT WINAPI HookedSetDisplayMode(
    void* pThis, DWORD dwWidth, DWORD dwHeight, DWORD dwBPP,
    DWORD dwRefreshRate, DWORD dwFlags)
{
    if (s_force60hz) dwRefreshRate = 60;
    return s_origSetDisplayMode(pThis, dwWidth, dwHeight, dwBPP, dwRefreshRate, dwFlags);
}

void installDDrawHook(bool force60hz) {
    s_force60hz = force60hz;

    HMODULE hDDraw = GetModuleHandleA("ddraw.dll");
    if (!hDDraw) hDDraw = LoadLibraryA("ddraw.dll");
    if (!hDDraw) return;

    auto fnCreate = reinterpret_cast<HRESULT(WINAPI*)(GUID*, IDirectDraw**, IUnknown*)>(
        GetProcAddress(hDDraw, "DirectDrawCreate"));
    if (!fnCreate) return;

    IDirectDraw* pDD = nullptr;
    if (FAILED(fnCreate(nullptr, &pDD, nullptr))) return;

    auto hookSlot = [](void* pIface, int slot) {
        void** vtable = *reinterpret_cast<void***>(pIface);
        if (vtable[slot] == reinterpret_cast<void*>(HookedSetDisplayMode)) return;
        if (!s_origSetDisplayMode)
            s_origSetDisplayMode = reinterpret_cast<SetDisplayModeFn>(vtable[slot]);
        DWORD oldProt;
        VirtualProtect(&vtable[slot], sizeof(void*), PAGE_READWRITE, &oldProt);
        vtable[slot] = reinterpret_cast<void*>(HookedSetDisplayMode);
        VirtualProtect(&vtable[slot], sizeof(void*), oldProt, &oldProt);
    };

    IDirectDraw2* pDD2 = nullptr;
    if (SUCCEEDED(pDD->QueryInterface(IID_IDirectDraw2, reinterpret_cast<void**>(&pDD2)))) {
        hookSlot(pDD2, 21);
        pDD2->Release();
    }
    IDirectDraw7* pDD7 = nullptr;
    if (SUCCEEDED(pDD->QueryInterface(IID_IDirectDraw7, reinterpret_cast<void**>(&pDD7)))) {
        hookSlot(pDD7, 21);
        pDD7->Release();
    }

    pDD->Release();
}
