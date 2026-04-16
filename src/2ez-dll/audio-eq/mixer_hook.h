#pragma once

/**
 * Windows Mixer API hook
 *
 * Intercepts winmm.dll mixer functions to fake SoundBlaster BASS/TREBLE
 * controls (normally provided by the EMU10K1 DSP). When the game sets values, maps them to dB gain
 * and updates the software biquad EQ filters.
 */

namespace MixerHook {
    void install(float gainMultiplier = 1.0f);
}
