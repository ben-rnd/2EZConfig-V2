#pragma once

/**
 * Shelf filter implementations for bass/treble EQ
 *
 * Uses the Biquad struct and supports both:
 * - First-order shelves (6 dB/oct, gentler approximation) - b2=a2=0
 * - Second-order shelves (12 dB/oct, closer to EMU10K1 DSP behaviour)
 *
 * Each instance holds per-channel state (delay line), so
 * stereo audio needs two instances per filter band.
 *
 * Implements Transposed Direct Form II:
 *   out = b0*in + z1
 *   z1  = b1*in - a1*out + z2
 *   z2  = b2*in - a2*out
 *
 * where b0/b1/b2 are feedforward (numerator) coefficients,
 * a1/a2 are feedback (denominator) coefficients, and z1/z2
 * are the delay line (filter memory between samples).
 *
 * Standard naming from the transfer function:
 *   H(z) = (b0 + b1*z^-1 + b2*z^-2) / (1 + a1*z^-1 + a2*z^-2)
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // Feedforward (numerator) coefficients — shape the frequency response
    double b0, b1, b2;
    // Feedback (denominator) coefficients — control resonance/rolloff
    double a1, a2;
    // Delay line — per-channel filter memory between consecutive samples
    double z1, z2;
} Biquad;

void biquad_reset(Biquad* filter);

// First-order shelves (6 dB/oct) — gentler, softer tone control
void biquad_lowShelf1(Biquad* filter, double sampleRate, double freq, double gainDb);
void biquad_highShelf1(Biquad* filter, double sampleRate, double freq, double gainDb);

// Second-order shelves (12 dB/oct) — closer to EMU10K1 DSP behaviour
void biquad_lowShelf2(Biquad* filter, double sampleRate, double freq, double gainDb);
void biquad_highShelf2(Biquad* filter, double sampleRate, double freq, double gainDb);

// Copy coefficients from src to dst without touching the delay line.
// Used to update filter response in real-time without audible glitches.
static inline void biquad_copyCoeffs(Biquad* dst, const Biquad* src) {
    dst->b0 = src->b0; dst->b1 = src->b1; dst->b2 = src->b2;
    dst->a1 = src->a1; dst->a2 = src->a2;
}

// Process a single sample through the filter (Transposed Direct Form II).
// Works for both first-order (b2=a2=0) and second-order filters.
static inline double biquad_process(Biquad* filter, double in) {
    double out = filter->b0 * in + filter->z1;               // apply feedforward + recall delay
    filter->z1 = filter->b1 * in - filter->a1 * out + filter->z2;     // update delay line slot 1
    filter->z2 = filter->b2 * in - filter->a2 * out;              // update delay line slot 2
    return out;
}

#ifdef __cplusplus
}
#endif
