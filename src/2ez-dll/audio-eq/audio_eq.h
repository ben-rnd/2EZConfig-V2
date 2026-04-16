#pragma once

/**
 * AudioEQ - Software bass/treble via WASAPI interception
 *
 * On Vista+, hooks IAudioRenderClient::GetBuffer/ReleaseBuffer to apply
 * shelf EQ filters in-place on the final mixed audio before output.
 * On XP (no WASAPI), gracefully disabled — mixer hooks still fake the
 * controls so the game doesn't crash, but no audio processing occurs.
 *
 * Reads settings from SettingsManager:
 *   audio_eq              (bool)  — master enable
 *   audio_eq_gain         (float) — gain multiplier (1.0 = SoundBlaster levels, clamped 0-5)
 *   audio_eq_second_order (bool)  — true = 12 dB/oct, false = 6 dB/oct
 */

class SettingsManager;

namespace AudioEQ {
    void install(SettingsManager* settings);
}
