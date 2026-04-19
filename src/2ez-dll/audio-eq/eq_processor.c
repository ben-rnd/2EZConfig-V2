#include "eq_processor.h"
#include "biquad.h"

static const double BASS_CROSSOVER_HZ   = 320.0;
static const double TREBLE_CROSSOVER_HZ = 5000.0;
static const double INT16_MAX_D =  32767.0;
static const double INT16_MIN_D = -32768.0;

static Biquad g_bassL, g_bassR;
static Biquad g_trebleL, g_trebleR;

static Biquad g_bassCoeffs;
static Biquad g_trebleCoeffs;
static int    g_bassEnabled = 0;
static int    g_trebleEnabled = 0;
static int    g_secondOrder = 0;
static double g_sampleRate = 44100.0;

static void syncCoefficients(void) {
    biquad_copyCoeffs(&g_bassL,   &g_bassCoeffs);
    biquad_copyCoeffs(&g_bassR,   &g_bassCoeffs);
    biquad_copyCoeffs(&g_trebleL, &g_trebleCoeffs);
    biquad_copyCoeffs(&g_trebleR, &g_trebleCoeffs);
}

void EqProcessor_init(double sampleRate, int secondOrder) {
    g_sampleRate = sampleRate;
    g_secondOrder = secondOrder;
    biquad_reset(&g_bassL);    biquad_reset(&g_bassR);
    biquad_reset(&g_trebleL);  biquad_reset(&g_trebleR);
    biquad_reset(&g_bassCoeffs);
    biquad_reset(&g_trebleCoeffs);
}

void EqProcessor_updateBass(double gainDb) {
    if (gainDb == 0.0) {
        g_bassEnabled = 0;
        biquad_reset(&g_bassL); biquad_reset(&g_bassR);
    } else {
        if (g_secondOrder)
            biquad_lowShelf2(&g_bassCoeffs, g_sampleRate, BASS_CROSSOVER_HZ, gainDb);
        else
            biquad_lowShelf1(&g_bassCoeffs, g_sampleRate, BASS_CROSSOVER_HZ, gainDb);
        g_bassEnabled = 1;
    }
}

void EqProcessor_updateTreble(double gainDb) {
    if (gainDb == 0.0) {
        g_trebleEnabled = 0;
        biquad_reset(&g_trebleL); biquad_reset(&g_trebleR);
    } else {
        if (g_secondOrder)
            biquad_highShelf2(&g_trebleCoeffs, g_sampleRate, TREBLE_CROSSOVER_HZ, gainDb);
        else
            biquad_highShelf1(&g_trebleCoeffs, g_sampleRate, TREBLE_CROSSOVER_HZ, gainDb);
        g_trebleEnabled = 1;
    }
}

void EqProcessor_processInt16Stereo(short* samples, unsigned int numFrames) {
    unsigned int i;
    double L, R;

    if (!g_bassEnabled && !g_trebleEnabled) return;

    syncCoefficients();
    for (i = 0; i < numFrames; i++) {
        L = (double)samples[i * 2];
        R = (double)samples[i * 2 + 1];
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

void EqProcessor_processFloat32Stereo(float* samples, unsigned int numFrames) {
    unsigned int i;
    double L, R;

    if (!g_bassEnabled && !g_trebleEnabled) return;

    syncCoefficients();
    for (i = 0; i < numFrames; i++) {
        L = (double)samples[i * 2];
        R = (double)samples[i * 2 + 1];
        if (g_bassEnabled)   { L = biquad_process(&g_bassL, L); R = biquad_process(&g_bassR, R); }
        if (g_trebleEnabled) { L = biquad_process(&g_trebleL, L); R = biquad_process(&g_trebleR, R); }
        samples[i * 2]     = (float)L;
        samples[i * 2 + 1] = (float)R;
    }
}
