#include "hypersonik_hook.h"
#include "hooks.h"
#include "logger.h"

extern "C" void ds_buffer_set_master_scale(double scale);

#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
#include <objbase.h>
#include <mmdeviceapi.h>

extern "C" const GUID CLSID_MMDeviceEnumerator;
extern "C" const GUID IID_IMMDeviceEnumerator;

// Hypersonik entry points (defined in ds-api.c)
extern "C" {
    HRESULT __stdcall ds_api_create(const GUID*, IDirectSound**, IUnknown*);
    HRESULT __stdcall ds_api_create8(const GUID*, IDirectSound8**, IUnknown*);
}

typedef HRESULT (WINAPI *PFN_DirectSoundCreate)(const GUID*, IDirectSound**, IUnknown*);
typedef HRESULT (WINAPI *PFN_DirectSoundCreate8)(const GUID*, IDirectSound8**, IUnknown*);

static PFN_DirectSoundCreate  g_origDSCreate = nullptr;
static PFN_DirectSoundCreate8 g_origDSCreate8 = nullptr;
static bool g_active = false;

static HRESULT WINAPI Hooked_DirectSoundCreate(
        const GUID* guid, IDirectSound** out, IUnknown* outer)
{
    return ds_api_create(guid, out, outer);
}

static HRESULT WINAPI Hooked_DirectSoundCreate8(
        const GUID* guid, IDirectSound8** out, IUnknown* outer)
{
    return ds_api_create8(guid, out, outer);
}

bool Hypersonik::install() {
    // Check if WASAPI is available (Vista+)
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    IMMDeviceEnumerator* pEnum = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr,
                                  CLSCTX_ALL, IID_IMMDeviceEnumerator,
                                  (void**)&pEnum);
    if (FAILED(hr) || !pEnum) {
        Logger::info("[Hypersonik] No WASAPI available - using native DirectSound");
        return false;
    }
    pEnum->Release();

    // Hook DirectSoundCreate and DirectSoundCreate8
    if (!hook_create_api(L"dsound.dll", "DirectSoundCreate",
                         (void*)Hooked_DirectSoundCreate, (void**)&g_origDSCreate)) {
        Logger::warn("[Hypersonik] Failed to hook DirectSoundCreate");
        return false;
    }

    hook_create_api(L"dsound.dll", "DirectSoundCreate8",
                    (void*)Hooked_DirectSoundCreate8, (void**)&g_origDSCreate8);

    g_active = true;
    Logger::info("[Hypersonik] DirectSound redirected to Hypersonik");
    return true;
}

bool Hypersonik::isActive() {
    return g_active;
}

void Hypersonik::setMasterVolume(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    ds_buffer_set_master_scale(256.0 * percent / 100.0);
}
