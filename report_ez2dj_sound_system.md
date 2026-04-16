# EZ2DJ Sound System Analysis: Bass/Treble/Cutoff via Hardware Mixer

## Summary

Both EZ2DJ 2nd Trax and 6th Trax use the **Windows Multimedia Mixer API** (`mixerGetLineInfoA`, `mixerGetLineControlsA`, `mixerSetControlDetails`) to manipulate **hardware bass and treble controls** exposed by the sound card driver. These controls only exist on SoundBlaster (and some other Creative Labs) sound cards. On modern/generic sound cards, the mixer controls simply don't exist, so the features silently fail.

---

## Architecture Overview

### KSNDMixer Class (Shared across versions)

The engine uses a `KSNDMixer` singleton that wraps the Windows Mixer API:

1. **`KSNDMixer::init()`** - Opens the system mixer device via `mixerOpen()`
2. **`KSNDMixer::createMixerControl(componentType, controlType)`** - Creates a handle to a specific mixer line control
   - `componentType` indexes into a table of `MIXERLINE_COMPONENTTYPE_*` values (DST_SPEAKERS=0, SRC_WAVEOUT=1, etc.)
   - `controlType` indexes into a table of `MIXERCONTROL_CONTROLTYPE_*` values (VOLUME=0, BASS=1, TREBLE=2, EQUALIZER=3, etc.)
   - Uses `MIXER_GETLINECONTROLSF_ONEBYTYPE` flag (2) to query by control type
   - Returns a 28-byte struct: `{HMIXER, dwControlID, componentIdx, controlIdx, minValue, maxValue, steps}`
3. **Get/Set value** - Reads/writes the control value via `mixerGetControlDetailsA` / `mixerSetControlDetails`

### Lookup Tables (Identical in both versions)

**Component Types** (`dword_4ED644` / `dword_47FFE4`):
| Index | Value | Constant |
|-------|-------|----------|
| 0 | 0x0004 | MIXERLINE_COMPONENTTYPE_DST_SPEAKERS |
| 1 | 0x1008 | MIXERLINE_COMPONENTTYPE_SRC_WAVEOUT |
| 2 | 0x1005 | MIXERLINE_COMPONENTTYPE_SRC_COMPACTDISC |
| 3 | 0x1004 | MIXERLINE_COMPONENTTYPE_SRC_SYNTHESIZER |
| 4 | 0x1002 | MIXERLINE_COMPONENTTYPE_SRC_MICROPHONE |
| 5 | 0x1001 | MIXERLINE_COMPONENTTYPE_SRC_LINE |
| 6 | 0x1009 | MIXERLINE_COMPONENTTYPE_SRC_AUXILIARY |
| 7 | 0x1000 | MIXERLINE_COMPONENTTYPE_SRC_UNDEFINED |

**Control Types** (`dword_4ED664` / `dword_480004`):
| Index | Value | Constant |
|-------|-------|----------|
| 0 | 0x50030001 | MIXERCONTROL_CONTROLTYPE_VOLUME |
| 1 | 0x50030002 | MIXERCONTROL_CONTROLTYPE_BASS |
| 2 | 0x50030003 | MIXERCONTROL_CONTROLTYPE_TREBLE |
| 3 | 0x50030004 | MIXERCONTROL_CONTROLTYPE_EQUALIZER |
| 4 | 0x50030000 | MIXERCONTROL_CONTROLTYPE_FADER |
| 5 | 0x40020001 | MIXERCONTROL_CONTROLTYPE_PAN |
| 6 | 0x40020002 | MIXERCONTROL_CONTROLTYPE_QSOUNDPAN |
| 7 | 0x40020000 | MIXERCONTROL_CONTROLTYPE_SLIDER |
| 8 | 0x20010002 | MIXERCONTROL_CONTROLTYPE_ONOFF |
| 9 | 0x20010004 | MIXERCONTROL_CONTROLTYPE_MONO |
| 10 | 0x20010003 | MIXERCONTROL_CONTROLTYPE_MUTE |
| 11 | 0x20010005 | MIXERCONTROL_CONTROLTYPE_LOUDNESS |
| 12 | 0x20010001 | MIXERCONTROL_CONTROLTYPE_BOOLEAN |

---

## 2nd Trax: Cutoff System

### Initialization (`KDirector::vSetup` at 0x4353D0)

On startup, creates mixer controls for DST_SPEAKERS and sets initial levels:
- **VOLUME** (comp=0, ctrl=0) -> set to **57194** (~87% of 65535)
- **TREBLE** (comp=0, ctrl=2) -> set to **43690** (~67%)
- **BASS** (comp=0, ctrl=1) -> set to **19065** (~29%)

Then immediately destroys the TREBLE and BASS controls (one-shot initialization).

### Gameplay Cutoff (`EZ2DJMainGameLayer::ctor` at 0x43A790)

During gameplay initialization:
- Creates persistent TREBLE control -> stored at object offset `+0x748` (self[466])
- Creates persistent BASS control -> stored at object offset `+0x74C` (self[467])
- Sets initial cutoff value to 128 (center) in PlayerNoteData[14]

### Cutoff Adjustment (`applyCutoff` at 0x43B6F0)

The cutoff value (0-256) controls the bass/treble balance:

```
cutoffValue = 0..256  (128 = center)

BASS level (self[467]):
  If (256 - cutoffValue) > 128:  BASS = 30935 * (256 - cutoffValue - 128) / 128 + 19065
  Else:                          BASS = 19065 (minimum)
  Range: 19065 (min) to 50000 (max)

TREBLE level (self[466]):
  If cutoffValue > 128:  TREBLE = 11121 * (cutoffValue - 128) / 128 + 43690
  Else:                  TREBLE = 43690 (minimum)
  Range: 43690 (min) to 54811 (max)
```

- **Cutoff=0**: Maximum bass (50000), default treble (43690) - "warm/bassy" 
- **Cutoff=128**: Default bass (19065), default treble (43690) - center
- **Cutoff=256**: Default bass (19065), maximum treble (54811) - "bright/trebly"

### Gameplay Controls

Commands 11/12 in `handleModifierCommand` adjust the cutoff by +/-7 per step, with snap-to-center behavior when crossing the midpoint (15-frame cooldown at center).

---

## 6th Trax: Bass/Treble Boost System

### Initialization (`KDirector__initMixerLevels` at 0x4136C0)

On startup, creates mixer controls for DST_SPEAKERS and sets initial levels:
- **VOLUME** (comp=0, ctrl=0) -> set to **50000** (~76%)
- **TREBLE** (comp=0, ctrl=2) -> set to **43690** (~67%)
- **BASS** (comp=0, ctrl=1) -> set to **19065** (~29%)

Saves original hardware values before overwriting.

### Gameplay Controls

6th Trax has the same cutoff system as 2nd Trax (same function at 0x4179C0), **plus** discrete bass/treble boost toggles:

**Bass Boost (Command 27)** - `InGameEffector__setBassBoost` at 0x417920:
- State stored in `dword_88A808`
- **ON**: Sets BASS mixer control to **30037** (~46%)
- **OFF**: Sets BASS mixer control to **19065** (~29%)
- Boost delta: +10972 units

**Treble Boost (Command 28)** - `InGameEffector__setTrebleBoost` at 0x417970:
- State stored in `dword_88A810`
- **ON**: Sets TREBLE mixer control to **54811** (~84%)
- **OFF**: Sets TREBLE mixer control to **43690** (~67%)
- Boost delta: +11121 units

### UI Assets

The InGameEffector loads toggle indicator textures:
- `System\InGameEffector\effector_bass_on.bmp` / `effector_bass_off.bmp`
- `System\InGameEffector\effector_treble_on.bmp` / `effector_treble_off.bmp`

---

## Why It Only Works With SoundBlaster

The Windows Mixer API exposes **hardware-level** controls. When the game calls:
```
mixerGetLineInfoA(hMixer, &mixerLine, MIXER_GETLINEINFOF_COMPONENTTYPE)
```
with `dwComponentType = MIXERLINE_COMPONENTTYPE_DST_SPEAKERS`, it asks for the master output line. Then:
```
mixerGetLineControlsA(hMixer, &mixerLineControls, MIXER_GETLINECONTROLSF_ONEBYTYPE)  
```
with `dwControlID = MIXERCONTROL_CONTROLTYPE_BASS` (0x50030002) asks for a bass fader control on that line.

**SoundBlaster cards** expose these BASS/TREBLE controls because their hardware DSP includes a parametric EQ. **Modern sound cards** (Realtek, Intel HD Audio, etc.) typically only expose VOLUME and MUTE on the master output - they do NOT expose BASS/TREBLE as mixer controls. When `mixerGetLineControlsA` fails (returns non-zero), `createMixerControl` returns NULL, the control pointer is never set, and all subsequent `if (control != NULL)` checks skip the mixer operations.

---

## Replacement Strategy: Software DSP via DirectSound

Since the game already uses DirectSound for audio playback, the most practical replacement is to apply **software-based EQ** to the DirectSound buffers. Options:

### Option 1: DirectSound Primary Buffer + IDirectSoundFXParamEq (DirectX 8+)
- Apply `DSFX_PARAMEQ` effect to the primary buffer
- Supports center frequency, bandwidth, and gain parameters
- Pros: Native DirectX, no external dependencies
- Cons: Requires DirectX 8+ (the game targets DX7)

### Option 2: Hook DirectSound Buffer Write and Apply Biquad Filter
- Intercept the final mix via the primary buffer or a secondary buffer
- Apply a software biquad filter (low-shelf for bass, high-shelf for treble)
- Use the **same value ranges** (19065-50000 for bass, 43690-54811 for treble) mapped to dB gain
- Pros: Works with any DirectX version, full control
- Cons: CPU overhead (minimal for a simple biquad), requires hooking

### Option 3: Replace `mixerSetControlDetails` with Software EQ in DLL Hook
- Hook the WINMM mixer functions in the game's import table
- When the game calls `mixerSetControlDetails` for BASS/TREBLE, instead apply a software EQ filter
- Pros: Zero changes to game logic, transparent replacement
- Cons: More complex hook architecture

### Recommended Approach: Option 3

Since EZ2Config already has a DLL injection framework (`ddraw` wrapper), the cleanest approach is:
1. **Hook the mixer API imports** in the game's IAT (Import Address Table)
2. Let `mixerOpen` succeed (or fake success)  
3. Let `mixerGetLineControlsA` return success for BASS/TREBLE control queries (return a fake control ID)
4. When `mixerSetControlDetails` is called with the fake control IDs, apply a **software biquad filter** to the DirectSound primary buffer
5. Map the game's value range (0-65535) to appropriate dB gain for the filter

### Filter Design

For bass: **Low-shelf filter** at ~300 Hz
- Game value 19065 (default) -> 0 dB gain (flat)
- Game value 50000 (max boost) -> ~+6 dB gain

For treble: **High-shelf filter** at ~3000 Hz  
- Game value 43690 (default) -> 0 dB gain (flat)
- Game value 54811 (max boost) -> ~+3 dB gain

These are simple IIR biquad filters that can be computed in real-time with negligible CPU cost.

---

## Key Addresses Summary

### 2nd Trax (EZ2DJ2nd_crk.exe)
| Function | Address | Purpose |
|----------|---------|---------|
| `KSNDMixer::init` | 0x428ED0 | Opens system mixer device |
| `KSNDMixer::createMixerControl` | 0x429130 | Creates handle to a mixer control |
| `KSNDMixer::destroyMixerControl` | 0x4293A0 | Destroys a mixer control handle |
| `KMixerControl::setVolume` | 0x4289F0 | Sets mixer control value |
| `KSNDMixer::getVolume` | 0x428940 | Gets mixer control value |
| `KDirector::vSetup` | 0x4353D0 | Initial mixer level setup |
| `EZ2DJMainGameLayer::ctor` | 0x43A790 | Creates TREBLE+BASS controls for gameplay |
| `applyCutoff` | 0x43B6F0 | Adjusts bass/treble balance (cutoff 0-256) |
| `adjustCutoffWithSnap` | 0x43B880 | Gameplay cutoff step adjustment |
| `handleModifierCommand` | 0x43BA00 | Dispatches gameplay modifiers |
| Component type table | 0x4ED644 | MIXERLINE_COMPONENTTYPE_* lookup |
| Control type table | 0x4ED664 | MIXERCONTROL_CONTROLTYPE_* lookup |

### 6th Trax (EZ2DJ6TH.EXE)
| Function | Address | Purpose |
|----------|---------|---------|
| `KSNDMixer::init` | 0x40E9E0 | Opens system mixer device |
| `KSNDMixer::createMixerControl` | 0x40EAA0 | Creates handle to a mixer control |
| `KSNDMixer::destroyMixerControl` | 0x40EBF0 | Destroys a mixer control handle |
| `KMixerControl::setValue` | 0x40E8F0 | Sets mixer control value |
| `KMixerControl::getValue` | 0x40E890 | Gets mixer control value |
| `KDirector::initMixerLevels` | 0x4136C0 | Initial mixer level setup |
| `MainGameLayer::ctor` | 0x416650 | Creates TREBLE+BASS controls for gameplay |
| `applyCutoff` | 0x4179C0 | Adjusts bass/treble balance (cutoff 0-256) |
| `setBassBoost` | 0x417920 | Bass boost toggle (19065/30037) |
| `setTrebleBoost` | 0x417970 | Treble boost toggle (43690/54811) |
| `handleModifierCommand` | 0x417B40 | Dispatches gameplay modifiers |
| Component type table | 0x47FFE4 | MIXERLINE_COMPONENTTYPE_* lookup |
| Control type table | 0x480004 | MIXERCONTROL_CONTROLTYPE_* lookup |

---

## Value Reference

| Setting | Default | Boosted/Max | % of 65535 (default) | % (boosted) |
|---------|---------|-------------|---------------------|-------------|
| VOLUME (2nd) | 57194 | - | 87.3% | - |
| VOLUME (6th) | 50000 | - | 76.3% | - |
| BASS | 19065 | 30037 (6th boost) / 50000 (2nd max) | 29.1% | 45.8% / 76.3% |
| TREBLE | 43690 | 54811 (6th boost/2nd max) | 66.7% | 83.6% |
