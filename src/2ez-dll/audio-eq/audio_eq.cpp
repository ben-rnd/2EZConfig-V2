/**
 * AudioEQ - WASAPI-level bass/treble processing
 *
 * On Vista+, DirectSound is emulated through WASAPI. We hook
 * IAudioRenderClient::GetBuffer and ::ReleaseBuffer to process
 * the final mixed audio in-place before it reaches the speakers.
 *
 * This is the same point in the pipeline where the SoundBlaster's
 * EMU10K1 DSP would process the audio - after all mixing, before output.
 *
 * When Hypersonik is active, EQ is applied inside its audio thread
 * instead of via WASAPI hooks to avoid conflicts.
 *
 * On XP (no WASAPI), the feature is gracefully disabled.
 *
 * The EQ uses shelf filters, Supports first-order (6 dB/oct, gentler)
 * or second-order (12 dB/oct, closer to EMU10K1) via a user toggle.
 */

#include "audio_eq.h"
#include "eq_processor.h"
#include "hypersonik_hook.h"
#include "mixer_hook.h"
#include "hooks.h"
#include "logger.h"
#include "settings.h"

#include <windows.h>
#include <mmsystem.h>

// WASAPI headers (available on Vista+ but we load dynamically)
#include <mmdeviceapi.h>
#include <audioclient.h>

// GUID declarations (defined in Hypersonik's guid.c)
extern "C" const GUID CLSID_MMDeviceEnumerator;
extern "C" const GUID IID_IMMDeviceEnumerator;
extern "C" const GUID IID_IAudioClient;
extern "C" const GUID IID_IAudioRenderClient;

// ---------------------------------------------------------------------------
// WASAPI vtable indices
// ---------------------------------------------------------------------------

namespace WasapiVT {
    enum IAudioRenderClient {
        GetBuffer = 3,      // after QI, AddRef, Release
        ReleaseBuffer = 4
    };
}

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

typedef HRESULT (STDMETHODCALLTYPE *PFN_GetBuffer)(
    IAudioRenderClient* self, UINT32 NumFramesRequested, BYTE** ppData);

typedef HRESULT (STDMETHODCALLTYPE *PFN_ReleaseBuffer)(
    IAudioRenderClient* self, UINT32 NumFramesWritten, DWORD dwFlags);

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static PFN_GetBuffer      g_origGetBuffer = nullptr;
static PFN_ReleaseBuffer  g_origReleaseBuffer = nullptr;

// Buffer pointer captured from GetBuffer, processed in ReleaseBuffer
static BYTE* g_pendingBuffer = nullptr;

// Audio format of the WASAPI endpoint (discovered at init)
static WORD g_channels = 2;
static WORD g_bitsPerSample = 16;
static DWORD g_sampleRate = 44100;
static bool g_isFloat = false;

// ---------------------------------------------------------------------------
// WASAPI hooks (legacy path - not used when Hypersonik is active)
// ---------------------------------------------------------------------------

static HRESULT STDMETHODCALLTYPE Hooked_GetBuffer(
    IAudioRenderClient* self, UINT32 NumFramesRequested, BYTE** ppData)
{
    HRESULT hr = g_origGetBuffer(self, NumFramesRequested, ppData);
    if (SUCCEEDED(hr) && ppData && *ppData) {
        g_pendingBuffer = *ppData;
    } else {
        g_pendingBuffer = nullptr;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE Hooked_ReleaseBuffer(
    IAudioRenderClient* self, UINT32 NumFramesWritten, DWORD dwFlags)
{
    if (g_pendingBuffer && NumFramesWritten > 0 &&
        !(dwFlags & AUDCLNT_BUFFERFLAGS_SILENT))
    {
        Logger::infoOnce("[AudioEQ] EQ processing active");
        if (g_isFloat && g_channels >= 2) {
            EqProcessor_processFloat32Stereo((float*)g_pendingBuffer, NumFramesWritten);
        } else if (!g_isFloat && g_bitsPerSample == 16 && g_channels >= 2) {
            EqProcessor_processInt16Stereo((short*)g_pendingBuffer, NumFramesWritten);
        }
    }

    g_pendingBuffer = nullptr;
    return g_origReleaseBuffer(self, NumFramesWritten, dwFlags);
}

// ---------------------------------------------------------------------------
// WASAPI setup - discover vtable and hook
// ---------------------------------------------------------------------------

static bool setupWasapiHooks() {
    // Initialize COM (may already be initialized by DirectSound)
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // Create device enumerator
    IMMDeviceEnumerator* pEnum = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr,
                                  CLSCTX_ALL, IID_IMMDeviceEnumerator,
                                  (void**)&pEnum);
    if (FAILED(hr) || !pEnum) {
        Logger::info("[AudioEQ] No WASAPI available - EQ disabled");
        return false;
    }

    // Get default render device
    IMMDevice* pDevice = nullptr;
    hr = pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    if (FAILED(hr) || !pDevice) {
        pEnum->Release();
        Logger::warn("[AudioEQ] No default audio endpoint");
        return false;
    }

    // Activate audio client
    IAudioClient* pClient = nullptr;
    hr = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr, (void**)&pClient);
    if (FAILED(hr) || !pClient) {
        pDevice->Release(); pEnum->Release();
        Logger::warn("[AudioEQ] Failed to activate audio client");
        return false;
    }

    // Get the mix format to know what we'll be processing
    WAVEFORMATEX* pMixFmt = nullptr;
    hr = pClient->GetMixFormat(&pMixFmt);
    if (SUCCEEDED(hr) && pMixFmt) {
        g_channels = pMixFmt->nChannels;
        g_bitsPerSample = pMixFmt->wBitsPerSample;
        g_sampleRate = pMixFmt->nSamplesPerSec;
        g_isFloat = (pMixFmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ||
                    (pMixFmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE && pMixFmt->wBitsPerSample == 32);
        Logger::info("[AudioEQ] Mix format: " + std::to_string(g_sampleRate) + "Hz " +
                     std::to_string(g_bitsPerSample) + "bit " +
                     std::to_string(g_channels) + "ch" +
                     (g_isFloat ? " float" : " int"));
        CoTaskMemFree(pMixFmt);
    }

    // Initialize a temporary audio client to get a render client for vtable discovery
    WAVEFORMATEX tmpFmt = {};
    tmpFmt.wFormatTag = WAVE_FORMAT_PCM;
    tmpFmt.nChannels = 2;
    tmpFmt.nSamplesPerSec = 44100;
    tmpFmt.wBitsPerSample = 16;
    tmpFmt.nBlockAlign = 4;
    tmpFmt.nAvgBytesPerSec = 176400;

    hr = pClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0,
                              10000000, 0, &tmpFmt, nullptr);
    if (FAILED(hr)) {
        // Try with the mix format instead
        WAVEFORMATEX* pFmt = nullptr;
        pClient->GetMixFormat(&pFmt);
        if (pFmt) {
            hr = pClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0,
                                      10000000, 0, pFmt, nullptr);
            CoTaskMemFree(pFmt);
        }
        if (FAILED(hr)) {
            pClient->Release(); pDevice->Release(); pEnum->Release();
            Logger::warn("[AudioEQ] Failed to initialize audio client");
            return false;
        }
    }

    // Get render client to discover vtable
    IAudioRenderClient* pRender = nullptr;
    hr = pClient->GetService(IID_IAudioRenderClient, (void**)&pRender);
    if (FAILED(hr) || !pRender) {
        pClient->Release(); pDevice->Release(); pEnum->Release();
        Logger::warn("[AudioEQ] Failed to get render client");
        return false;
    }

    // Hook GetBuffer and ReleaseBuffer via vtable
    void** vtable = *(void***)pRender;
    bool hooked = false;

    if (hook_create(vtable[WasapiVT::IAudioRenderClient::GetBuffer],
                   (void*)Hooked_GetBuffer, (void**)&g_origGetBuffer)) {
        Logger::info("[AudioEQ] Hooked IAudioRenderClient::GetBuffer");
    }
    if (hook_create(vtable[WasapiVT::IAudioRenderClient::ReleaseBuffer],
                   (void*)Hooked_ReleaseBuffer, (void**)&g_origReleaseBuffer)) {
        Logger::info("[AudioEQ] Hooked IAudioRenderClient::ReleaseBuffer");
        hooked = true;
    }

    // Release discovery objects (hooks are on the function body, persist)
    pRender->Release();
    pClient->Release();
    pDevice->Release();
    pEnum->Release();

    return hooked;
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------

void AudioEQ::install(SettingsManager* settings) {
    auto& gs = settings->gameSettings();
    if (!gs.value("audio_eq", false)) return;

    float gainMultiplier = gs.value("audio_eq_gain", 1.0f);
    bool secondOrder = gs.value("audio_eq_second_order", false);

    Logger::info(std::string("[AudioEQ] Filter order: ") + (secondOrder ? "Second (12 dB/oct)" : "First (6 dB/oct)"));

    if (Hypersonik::isActive()) {
        // Hypersonik handles WASAPI directly — EQ is applied inside its audio thread
        EqProcessor_init(44100.0, secondOrder ? 1 : 0);
        MixerHook::install(gainMultiplier);
        Logger::info("[AudioEQ] Hypersonik active - EQ via embedded pipeline");
    } else {
        // Legacy path: hook WASAPI globally
        if (setupWasapiHooks()) {
            EqProcessor_init((double)g_sampleRate, secondOrder ? 1 : 0);
            MixerHook::install(gainMultiplier);
            Logger::info("[AudioEQ] WASAPI treble/bass control active");
        } else {
            Logger::warn("[AudioEQ] WASAPI treble/bass control unavailable.");
        }
    }
}
