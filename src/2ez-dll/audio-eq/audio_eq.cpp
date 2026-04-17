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
 * On XP (no WASAPI), the feature is gracefully disabled.
 *
 * The EQ uses shelf filters, Supports first-order (6 dB/oct, gentler)
 * or second-order (12 dB/oct, closer to EMU10K1) via a user toggle.
 */

#include "audio_eq.h"
#include "biquad.h"
#include "mixer_hook.h"
#include "hooks.h"
#include "logger.h"
#include "settings.h"

#include <windows.h>
#include <mmsystem.h>

// WASAPI headers (available on Vista+ but we load dynamically)
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

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

// EQ filter state (persistent across ReleaseBuffer calls for smooth processing)
static Biquad g_bassL, g_bassR;
static Biquad g_trebleL, g_trebleR;

// EQ coefficients and enabled state
static Biquad g_bassCoeffs;
static Biquad g_trebleCoeffs;
static bool   g_bassEnabled = false;
static bool   g_trebleEnabled = false;
static bool   g_secondOrder = false;

// ---------------------------------------------------------------------------
// Filter update (called from mixer hook)
// ---------------------------------------------------------------------------

// Shelf crossover frequencies. Within standard consumer tone-control ranges
// (bass 150-400 Hz, treble 1.5-7.5 kHz). Not derived from EMU10K1 coefficients
// directly — the real DSP uses precomputed lookup tables with no explicit frequency.
static constexpr double BASS_CROSSOVER_HZ   = 320.0;
static constexpr double TREBLE_CROSSOVER_HZ = 5000.0;

void AudioEQ_updateBass(double gainDb) {
    if (gainDb == 0.0) {
        g_bassEnabled = false;
        biquad_reset(&g_bassL); biquad_reset(&g_bassR);
    } else {
        if (g_secondOrder)
            biquad_lowShelf2(&g_bassCoeffs, (double)g_sampleRate, BASS_CROSSOVER_HZ, gainDb);
        else
            biquad_lowShelf1(&g_bassCoeffs, (double)g_sampleRate, BASS_CROSSOVER_HZ, gainDb);
        g_bassEnabled = true;
    }
}

void AudioEQ_updateTreble(double gainDb) {
    if (gainDb == 0.0) {
        g_trebleEnabled = false;
        biquad_reset(&g_trebleL); biquad_reset(&g_trebleR);
    } else {
        if (g_secondOrder)
            biquad_highShelf2(&g_trebleCoeffs, (double)g_sampleRate, TREBLE_CROSSOVER_HZ, gainDb);
        else
            biquad_highShelf1(&g_trebleCoeffs, (double)g_sampleRate, TREBLE_CROSSOVER_HZ, gainDb);
        g_trebleEnabled = true;
    }
}

// ---------------------------------------------------------------------------
// Audio processing
// ---------------------------------------------------------------------------

// Sync coefficients from the shared coeff structs into per-channel filter
// instances (preserves delay lines for glitch-free real-time updates).
static void syncCoefficients() {
    biquad_copyCoeffs(&g_bassL,   &g_bassCoeffs);
    biquad_copyCoeffs(&g_bassR,   &g_bassCoeffs);
    biquad_copyCoeffs(&g_trebleL, &g_trebleCoeffs);
    biquad_copyCoeffs(&g_trebleR, &g_trebleCoeffs);
}

static constexpr double INT16_MAX_D =  32767.0;
static constexpr double INT16_MIN_D = -32768.0;

static void processFloat32Stereo(float* samples, UINT32 numFrames) {
    syncCoefficients();
    for (UINT32 i = 0; i < numFrames; i++) {
        double L = (double)samples[i * 2];
        double R = (double)samples[i * 2 + 1];
        if (g_bassEnabled)   { L = biquad_process(&g_bassL, L); R = biquad_process(&g_bassR, R); }
        if (g_trebleEnabled) { L = biquad_process(&g_trebleL, L); R = biquad_process(&g_trebleR, R); }
        samples[i * 2]     = (float)L;
        samples[i * 2 + 1] = (float)R;
    }
}

static void processInt16Stereo(short* samples, UINT32 numFrames) {
    syncCoefficients();
    for (UINT32 i = 0; i < numFrames; i++) {
        double L = (double)samples[i * 2];
        double R = (double)samples[i * 2 + 1];
        if (g_bassEnabled)   { L = biquad_process(&g_bassL, L); R = biquad_process(&g_bassR, R); }
        if (g_trebleEnabled) { L = biquad_process(&g_trebleL, L); R = biquad_process(&g_trebleR, R); }
        if (L > INT16_MAX_D) L = INT16_MAX_D;
        if (L < INT16_MIN_D) L = INT16_MIN_D;
        if (R > INT16_MAX_D) R = INT16_MAX_D;
        if (R < INT16_MIN_D) R = INT16_MIN_D;
        samples[i * 2]     = (short)L;
        samples[i * 2 + 1] = (short)R;
    }
}

// ---------------------------------------------------------------------------
// WASAPI hooks
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
        !(dwFlags & AUDCLNT_BUFFERFLAGS_SILENT) &&
        (g_bassEnabled || g_trebleEnabled))
    {
        Logger::infoOnce("[AudioEQ] EQ processing active");
        if (g_isFloat && g_channels >= 2) {
            processFloat32Stereo((float*)g_pendingBuffer, NumFramesWritten);
        } else if (!g_isFloat && g_bitsPerSample == 16 && g_channels >= 2) {
            processInt16Stereo((short*)g_pendingBuffer, NumFramesWritten);
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
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
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
    hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&pClient);
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
    hr = pClient->GetService(__uuidof(IAudioRenderClient), (void**)&pRender);
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
    g_secondOrder = gs.value("audio_eq_second_order", false);

    biquad_reset(&g_bassL);    biquad_reset(&g_bassR);
    biquad_reset(&g_trebleL);  biquad_reset(&g_trebleR);
    biquad_reset(&g_bassCoeffs);
    biquad_reset(&g_trebleCoeffs);
    Logger::info(std::string("[AudioEQ] Filter order: ") + (g_secondOrder ? "Second (12 dB/oct)" : "First (6 dB/oct)"));

    // Only install mixer hooks if WASAPI succeeds
    if (setupWasapiHooks()) {
        MixerHook::install(gainMultiplier);
        Logger::info("[AudioEQ] WASAPI treble/bass control active");
    } else {
        Logger::warn("[AudioEQ] WASAPI treble/bass control unavailable.");
    }
}
