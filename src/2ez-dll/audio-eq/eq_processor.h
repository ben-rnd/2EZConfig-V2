#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void EqProcessor_init(double sampleRate, int secondOrder);
void EqProcessor_updateBass(double gainDb);
void EqProcessor_updateTreble(double gainDb);
void EqProcessor_processInt16Stereo(short* samples, unsigned int numFrames);
void EqProcessor_processFloat32Stereo(float* samples, unsigned int numFrames);

#ifdef __cplusplus
}
#endif
