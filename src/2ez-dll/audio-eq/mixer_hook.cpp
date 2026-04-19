/**
 * Windows Mixer API hook - fakes SoundBlaster BASS/TREBLE controls
 *
 * The game's KSNDMixer class queries the system mixer for BASS and TREBLE
 * fader controls on DST_SPEAKERS. Only SoundBlaster cards expose these.
 * This hook fakes their existence and translates value changes into
 * software EQ filter updates.
 *
 * Mixer API call flow the game uses:
 *   1. mixerGetNumDevs() -> returns count (must be >= 1)
 *   2. mixerOpen() -> opens mixer device 0
 *   3. mixerGetLineInfoA(MIXER_GETLINEINFOF_COMPONENTTYPE) -> finds DST_SPEAKERS
 *   4. mixerGetLineControlsA(MIXER_GETLINECONTROLSF_ONEBYTYPE) -> finds BASS/TREBLE
 *   5. mixerSetControlDetails() -> sets the value
 *   6. mixerGetControlDetailsA() -> reads the value
 *   7. mixerClose() -> closes mixer
 */

#include "mixer_hook.h"
#include "hooks.h"
#include "logger.h"

#include <windows.h>
#include <mmsystem.h>

#include "eq_processor.h"

// ---------------------------------------------------------------------------
// Constants matching the game's lookup tables
// ---------------------------------------------------------------------------

// These are already defined in mmeapi.h but we reference them explicitly
// MIXERCONTROL_CONTROLTYPE_VOLUME = 0x50030001
// MIXERCONTROL_CONTROLTYPE_BASS   = 0x50030002
// MIXERCONTROL_CONTROLTYPE_TREBLE = 0x50030003

// Fake control IDs we assign to our virtual controls
#define FAKE_CONTROL_ID_VOLUME  0xEE000001
#define FAKE_CONTROL_ID_BASS    0xEE000002
#define FAKE_CONTROL_ID_TREBLE  0xEE000003
#define FAKE_LINE_ID_SPEAKERS   0xEE010000

// Game's known value ranges (from reverse engineering the KSNDMixer class)
//   BASS:   19065 (default/off) to 50000 (max boost in 2nd Trax)
//   TREBLE: 43690 (default/off) to 54811 (max boost)
//   Mixer fader range: 0 to 65535 (standard Windows mixer unit range)

#define MIXER_FADER_MAX    65535
#define BASS_DEFAULT       19065
#define TREBLE_DEFAULT     43690
#define SHELF_DB_RANGE     12.0    // EMU10K1 DSP range: +/- 12 dB

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

typedef UINT  (WINAPI *PFN_mixerGetNumDevs)(void);
typedef MMRESULT (WINAPI *PFN_mixerOpen)(LPHMIXER, UINT, DWORD_PTR, DWORD_PTR, DWORD);
typedef MMRESULT (WINAPI *PFN_mixerClose)(HMIXER);
typedef MMRESULT (WINAPI *PFN_mixerGetLineInfoA)(HMIXEROBJ, LPMIXERLINEA, DWORD);
typedef MMRESULT (WINAPI *PFN_mixerGetLineControlsA)(HMIXEROBJ, LPMIXERLINECONTROLSA, DWORD);
typedef MMRESULT (WINAPI *PFN_mixerSetControlDetails)(HMIXEROBJ, LPMIXERCONTROLDETAILS, DWORD);
typedef MMRESULT (WINAPI *PFN_mixerGetControlDetailsA)(HMIXEROBJ, LPMIXERCONTROLDETAILS, DWORD);

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static PFN_mixerGetNumDevs        g_origGetNumDevs = nullptr;
static PFN_mixerOpen              g_origMixerOpen = nullptr;
static PFN_mixerClose             g_origMixerClose = nullptr;
static PFN_mixerGetLineInfoA      g_origGetLineInfo = nullptr;
static PFN_mixerGetLineControlsA  g_origGetLineControls = nullptr;
static PFN_mixerSetControlDetails g_origSetControlDetails = nullptr;
static PFN_mixerGetControlDetailsA g_origGetControlDetails = nullptr;

// Current fake control values (raw mixer units, 0-65535)
static DWORD g_bassValue = BASS_DEFAULT;
static DWORD g_trebleValue = TREBLE_DEFAULT;
static DWORD g_volumeValue = 50000;

// Gain multiplier (scales dB output, clamped 0.0-5.0)
static double g_gainMultiplier = 1.0;


// ---------------------------------------------------------------------------
// Value mapping: game mixer units -> dB gain
// ---------------------------------------------------------------------------

static double mapBassToDb(DWORD value) {
    // Game default is our 0 dB reference (flat relative to no-hardware).
    // Below default: cut. Above default: boost. Range: +/- SHELF_DB_RANGE dB.
    double db;
    if (value <= BASS_DEFAULT) {
        db = -SHELF_DB_RANGE * (1.0 - (double)value / (double)BASS_DEFAULT);
    } else {
        double ratio = (double)(value - BASS_DEFAULT) / (double)(MIXER_FADER_MAX - BASS_DEFAULT);
        db = SHELF_DB_RANGE * ratio;
    }
    return db * g_gainMultiplier;
}

static double mapTrebleToDb(DWORD value) {
    double db;
    if (value <= TREBLE_DEFAULT) {
        db = -SHELF_DB_RANGE * (1.0 - (double)value / (double)TREBLE_DEFAULT);
    } else {
        double ratio = (double)(value - TREBLE_DEFAULT) / (double)(MIXER_FADER_MAX - TREBLE_DEFAULT);
        db = SHELF_DB_RANGE * ratio;
    }
    return db * g_gainMultiplier;
}

// ---------------------------------------------------------------------------
// Hook implementations
// ---------------------------------------------------------------------------

static UINT WINAPI Hooked_mixerGetNumDevs(void) {
    UINT real = g_origGetNumDevs();
    if (real == 0) {
        Logger::info("[MixerHook] No mixer devices detected, reporting virtual device");
        return 1;
    }
    return real;
}

static MMRESULT WINAPI Hooked_mixerOpen(
    LPHMIXER phmx, UINT uMxId, DWORD_PTR dwCallback,
    DWORD_PTR dwInstance, DWORD fdwOpen)
{
    MMRESULT result = g_origMixerOpen(phmx, uMxId, dwCallback, dwInstance, fdwOpen);
    if (result == MMSYSERR_NOERROR) {
        Logger::info("[MixerHook] Real mixer opened successfully");
        return result;
    }

    // Real mixer failed - provide a fake handle
    Logger::info("[MixerHook] Real mixer open failed, providing fake handle");
    if (phmx) *phmx = (HMIXER)0xDEAD0001;
    return MMSYSERR_NOERROR;
}

static MMRESULT WINAPI Hooked_mixerClose(HMIXER hmx) {
    if (hmx == (HMIXER)0xDEAD0001) {
        return MMSYSERR_NOERROR;
    }
    return g_origMixerClose(hmx);
}

static MMRESULT WINAPI Hooked_mixerGetLineInfoA(
    HMIXEROBJ hmxobj, LPMIXERLINEA pmxl, DWORD fdwInfo)
{
    // Try the real mixer first
    MMRESULT result = MMSYSERR_ERROR;
    if (hmxobj != (HMIXEROBJ)0xDEAD0001) {
        result = g_origGetLineInfo(hmxobj, pmxl, fdwInfo);
    }

    if (result == MMSYSERR_NOERROR) return result;

    // Real mixer doesn't have this line - fake it for DST_SPEAKERS
    if ((fdwInfo & MIXER_GETLINEINFOF_QUERYMASK) == MIXER_GETLINEINFOF_COMPONENTTYPE) {
        if (pmxl && pmxl->dwComponentType == MIXERLINE_COMPONENTTYPE_DST_SPEAKERS) {
            memset(pmxl, 0, sizeof(MIXERLINEA));
            pmxl->cbStruct = sizeof(MIXERLINEA);
            pmxl->dwLineID = FAKE_LINE_ID_SPEAKERS;
            pmxl->dwComponentType = MIXERLINE_COMPONENTTYPE_DST_SPEAKERS;
            pmxl->cChannels = 2;
            pmxl->cControls = 3;
            lstrcpynA(pmxl->szShortName, "Volume", MIXER_SHORT_NAME_CHARS);
            lstrcpynA(pmxl->szName, "Master Volume", MIXER_LONG_NAME_CHARS);
            return MMSYSERR_NOERROR;
        }
    }

    return MMSYSERR_ERROR;
}

static MMRESULT WINAPI Hooked_mixerGetLineControlsA(
    HMIXEROBJ hmxobj, LPMIXERLINECONTROLSA pmxlc, DWORD fdwControls)
{
    // Try real mixer first (for cards that actually support bass/treble)
    MMRESULT result = MMSYSERR_ERROR;
    if (hmxobj != (HMIXEROBJ)0xDEAD0001) {
        result = g_origGetLineControls(hmxobj, pmxlc, fdwControls);
    }

    if (result == MMSYSERR_NOERROR) {
        Logger::infoOnce("[MixerHook] Real mixer controls detected, passing through");
        return result;
    }

    // Real mixer doesn't have this control - fake it
    if ((fdwControls & MIXER_GETLINECONTROLSF_QUERYMASK) != MIXER_GETLINECONTROLSF_ONEBYTYPE)
        return MMSYSERR_ERROR;

    if (!pmxlc || !pmxlc->pamxctrl)
        return MMSYSERR_ERROR;

    DWORD requestedType = pmxlc->dwControlID; // Actually dwControlType when using ONEBYTYPE
    LPMIXERCONTROLA pCtrl = pmxlc->pamxctrl;

    memset(pCtrl, 0, sizeof(MIXERCONTROLA));
    pCtrl->cbStruct = sizeof(MIXERCONTROLA);

    if (requestedType == MIXERCONTROL_CONTROLTYPE_BASS) {
        pCtrl->dwControlID = FAKE_CONTROL_ID_BASS;
        pCtrl->dwControlType = MIXERCONTROL_CONTROLTYPE_BASS;
        pCtrl->Bounds.dwMinimum = 0;
        pCtrl->Bounds.dwMaximum = MIXER_FADER_MAX;
        pCtrl->Metrics.cSteps = 16;
        lstrcpynA(pCtrl->szShortName, "Bass", MIXER_SHORT_NAME_CHARS);
        lstrcpynA(pCtrl->szName, "Bass", MIXER_LONG_NAME_CHARS);
        Logger::info("[MixerHook] Faked BASS control");
        return MMSYSERR_NOERROR;
    }

    if (requestedType == MIXERCONTROL_CONTROLTYPE_TREBLE) {
        pCtrl->dwControlID = FAKE_CONTROL_ID_TREBLE;
        pCtrl->dwControlType = MIXERCONTROL_CONTROLTYPE_TREBLE;
        pCtrl->Bounds.dwMinimum = 0;
        pCtrl->Bounds.dwMaximum = MIXER_FADER_MAX;
        pCtrl->Metrics.cSteps = 16;
        lstrcpynA(pCtrl->szShortName, "Treble", MIXER_SHORT_NAME_CHARS);
        lstrcpynA(pCtrl->szName, "Treble", MIXER_LONG_NAME_CHARS);
        Logger::info("[MixerHook] Faked TREBLE control");
        return MMSYSERR_NOERROR;
    }

    if (requestedType == MIXERCONTROL_CONTROLTYPE_VOLUME) {
        pCtrl->dwControlID = FAKE_CONTROL_ID_VOLUME;
        pCtrl->dwControlType = MIXERCONTROL_CONTROLTYPE_VOLUME;
        pCtrl->Bounds.dwMinimum = 0;
        pCtrl->Bounds.dwMaximum = MIXER_FADER_MAX;
        pCtrl->Metrics.cSteps = 16;
        lstrcpynA(pCtrl->szShortName, "Volume", MIXER_SHORT_NAME_CHARS);
        lstrcpynA(pCtrl->szName, "Master Volume", MIXER_LONG_NAME_CHARS);
        Logger::info("[MixerHook] Faked VOLUME control");
        return MMSYSERR_NOERROR;
    }

    return MMSYSERR_ERROR;
}

static MMRESULT WINAPI Hooked_mixerSetControlDetails(
    HMIXEROBJ hmxobj, LPMIXERCONTROLDETAILS pmxcd, DWORD fdwDetails)
{
    if (pmxcd && pmxcd->paDetails && pmxcd->cbDetails >= 4) {
        DWORD controlId = pmxcd->dwControlID;
        DWORD value = *(DWORD*)pmxcd->paDetails;

        if (controlId == FAKE_CONTROL_ID_BASS) {
            g_bassValue = value;
            double db = mapBassToDb(value);
            EqProcessor_updateBass(db);
            Logger::info("[MixerHook] Bass set to " + std::to_string(value) +
                        " (" + std::to_string(db) + " dB)");
            return MMSYSERR_NOERROR;
        }

        if (controlId == FAKE_CONTROL_ID_TREBLE) {
            g_trebleValue = value;
            double db = mapTrebleToDb(value);
            EqProcessor_updateTreble(db);
            Logger::info("[MixerHook] Treble set to " + std::to_string(value) +
                        " (" + std::to_string(db) + " dB)");
            return MMSYSERR_NOERROR;
        }

        if (controlId == FAKE_CONTROL_ID_VOLUME) {
            g_volumeValue = value;
            // Volume is handled by DirectSound natively, just track it
            return MMSYSERR_NOERROR;
        }
    }

    // Not one of our fake controls - pass through to real mixer
    if (hmxobj != (HMIXEROBJ)0xDEAD0001) {
        return g_origSetControlDetails(hmxobj, pmxcd, fdwDetails);
    }
    return MMSYSERR_NOERROR;
}

static MMRESULT WINAPI Hooked_mixerGetControlDetailsA(
    HMIXEROBJ hmxobj, LPMIXERCONTROLDETAILS pmxcd, DWORD fdwDetails)
{
    if (pmxcd && pmxcd->paDetails && pmxcd->cbDetails >= 4) {
        DWORD controlId = pmxcd->dwControlID;

        if (controlId == FAKE_CONTROL_ID_BASS) {
            *(DWORD*)pmxcd->paDetails = g_bassValue;
            return MMSYSERR_NOERROR;
        }
        if (controlId == FAKE_CONTROL_ID_TREBLE) {
            *(DWORD*)pmxcd->paDetails = g_trebleValue;
            return MMSYSERR_NOERROR;
        }
        if (controlId == FAKE_CONTROL_ID_VOLUME) {
            *(DWORD*)pmxcd->paDetails = g_volumeValue;
            return MMSYSERR_NOERROR;
        }
    }

    if (hmxobj != (HMIXEROBJ)0xDEAD0001) {
        return g_origGetControlDetails(hmxobj, pmxcd, fdwDetails);
    }
    return MMSYSERR_NOERROR;
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------

void MixerHook::install(float gainMultiplier) {
    // Clamp to safe range: 0.0 - 5.0 (5x = up to 30 dB boost, beyond that risks clipping badly)
    if (gainMultiplier < 0.0f) gainMultiplier = 0.0f;
    if (gainMultiplier > 5.0f) gainMultiplier = 5.0f;
    g_gainMultiplier = (double)gainMultiplier;
    Logger::info("[MixerHook] Gain multiplier: " + std::to_string(gainMultiplier) + "x");
    hook_create_api(L"winmm.dll", "mixerGetNumDevs",
                    (void*)Hooked_mixerGetNumDevs, (void**)&g_origGetNumDevs);
    hook_create_api(L"winmm.dll", "mixerOpen",
                    (void*)Hooked_mixerOpen, (void**)&g_origMixerOpen);
    hook_create_api(L"winmm.dll", "mixerClose",
                    (void*)Hooked_mixerClose, (void**)&g_origMixerClose);
    hook_create_api(L"winmm.dll", "mixerGetLineInfoA",
                    (void*)Hooked_mixerGetLineInfoA, (void**)&g_origGetLineInfo);
    hook_create_api(L"winmm.dll", "mixerGetLineControlsA",
                    (void*)Hooked_mixerGetLineControlsA, (void**)&g_origGetLineControls);
    hook_create_api(L"winmm.dll", "mixerSetControlDetails",
                    (void*)Hooked_mixerSetControlDetails, (void**)&g_origSetControlDetails);
    hook_create_api(L"winmm.dll", "mixerGetControlDetailsA",
                    (void*)Hooked_mixerGetControlDetailsA, (void**)&g_origGetControlDetails);

    Logger::info("[MixerHook] Installed winmm mixer hooks");
}
