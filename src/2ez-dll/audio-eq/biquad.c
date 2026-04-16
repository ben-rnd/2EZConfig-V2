/**
 * Shelf filter implementations for bass/treble EQ
 *
 * First-order (6 dB/oct):
 *   Softer, gentler tone control. Derived via bilinear transform of
 *   first-order analog shelf prototypes:
 *     Low shelf:  H(s) = (s + wc*sqrtA) / (s + wc/sqrtA)
 *     High shelf: H(s) = (s*sqrtA + wc) / (s/sqrtA + wc)
 *
 * Second-order (12 dB/oct):
 *   Closer match to the SoundBlaster Live! CT4670 (EMU10K1) DSP, which
 *   uses precomputed second-order biquad coefficient tables (verified from
 *   Linux ALSA driver emufx.c — 5 coefficients per gain step).
 *   Standard Audio EQ Cookbook second-order shelving design with Q = 0.707.
 *
 * Both use: A = 10^(gainDb/20), sqrtA = 10^(gainDb/40)
 *
 * Reference: Robert Bristow-Johnson, "Cookbook formulae for audio EQ biquad
 * filter coefficients" (Audio EQ Cookbook).
 * https://www.w3.org/2011/audio/audio-eq-cookbook.html
 */

#include "biquad.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Shelf Q factor for second-order filters.
// 0.707 (1/sqrt(2)) gives a Butterworth-like maximally-flat transition.
#define SHELF_Q 0.707

void biquad_reset(Biquad* filter) {
    filter->b0 = 1.0;
    filter->b1 = 0.0;
    filter->b2 = 0.0;
    filter->a1 = 0.0;
    filter->a2 = 0.0;
    filter->z1 = 0.0;
    filter->z2 = 0.0;
}

// ---------------------------------------------------------------------------
// First-order shelves (6 dB/oct)
// ---------------------------------------------------------------------------

void biquad_lowShelf1(Biquad* filter, double sampleRate, double freq, double gainDb) {
    double A = pow(10.0, gainDb / 20.0);
    double sqrtA = sqrt(A);
    double K = tan(M_PI * freq / sampleRate);

    double KsqrtA = K * sqrtA;
    double KdivsqrtA = K / sqrtA;
    double norm = 1.0 / (1.0 + KdivsqrtA);

    filter->b0 = (1.0 + KsqrtA) * norm;
    filter->b1 = (KsqrtA - 1.0) * norm;
    filter->b2 = 0.0;
    filter->a1 = (KdivsqrtA - 1.0) * norm;
    filter->a2 = 0.0;
}

void biquad_highShelf1(Biquad* filter, double sampleRate, double freq, double gainDb) {
    double A = pow(10.0, gainDb / 20.0);
    double sqrtA = sqrt(A);
    double K = tan(M_PI * freq / sampleRate);

    double norm = 1.0 / (1.0 / sqrtA + K);

    filter->b0 = (sqrtA + K) * norm;
    filter->b1 = (K - sqrtA) * norm;
    filter->b2 = 0.0;
    filter->a1 = (K - 1.0 / sqrtA) * norm;
    filter->a2 = 0.0;
}

// ---------------------------------------------------------------------------
// Second-order shelves (12 dB/oct) — Audio EQ Cookbook formulae
// ---------------------------------------------------------------------------

void biquad_lowShelf2(Biquad* filter, double sampleRate, double freq, double gainDb) {
    double A = pow(10.0, gainDb / 40.0);  // sqrt of amplitude (cookbook convention)
    double w0 = 2.0 * M_PI * freq / sampleRate;
    double cosw0 = cos(w0);
    double sinw0 = sin(w0);
    double alpha = sinw0 / (2.0 * SHELF_Q);
    double twoSqrtAalpha = 2.0 * sqrt(A) * alpha;

    double a0 = (A + 1.0) + (A - 1.0) * cosw0 + twoSqrtAalpha;
    double norm = 1.0 / a0;

    filter->b0 = (A * ((A + 1.0) - (A - 1.0) * cosw0 + twoSqrtAalpha)) * norm;
    filter->b1 = (2.0 * A * ((A - 1.0) - (A + 1.0) * cosw0))           * norm;
    filter->b2 = (A * ((A + 1.0) - (A - 1.0) * cosw0 - twoSqrtAalpha)) * norm;
    filter->a1 = (-2.0 * ((A - 1.0) + (A + 1.0) * cosw0))              * norm;
    filter->a2 = ((A + 1.0) + (A - 1.0) * cosw0 - twoSqrtAalpha)       * norm;
}

void biquad_highShelf2(Biquad* filter, double sampleRate, double freq, double gainDb) {
    double A = pow(10.0, gainDb / 40.0);
    double w0 = 2.0 * M_PI * freq / sampleRate;
    double cosw0 = cos(w0);
    double sinw0 = sin(w0);
    double alpha = sinw0 / (2.0 * SHELF_Q);
    double twoSqrtAalpha = 2.0 * sqrt(A) * alpha;

    double a0 = (A + 1.0) - (A - 1.0) * cosw0 + twoSqrtAalpha;
    double norm = 1.0 / a0;

    filter->b0 = (A * ((A + 1.0) + (A - 1.0) * cosw0 + twoSqrtAalpha))  * norm;
    filter->b1 = (-2.0 * A * ((A - 1.0) + (A + 1.0) * cosw0))           * norm;
    filter->b2 = (A * ((A + 1.0) + (A - 1.0) * cosw0 - twoSqrtAalpha))  * norm;
    filter->a1 = (2.0 * ((A - 1.0) - (A + 1.0) * cosw0))                * norm;
    filter->a2 = ((A + 1.0) - (A - 1.0) * cosw0 - twoSqrtAalpha)        * norm;
}
