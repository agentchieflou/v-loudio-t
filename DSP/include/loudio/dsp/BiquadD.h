#pragma once
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * Double-precision biquad for the EQs (EQ-8 #136, EQ-3 #145; spike
 * decision §5.3): the blueprint asks for 64-bit state in low-frequency
 * bands to avoid quantisation noise and limit cycles, and a transposed
 * direct form II (TDF2) for stability under fast frequency/Q modulation.
 * Everything else in the library stays on the float SimpleBiquad.
 *
 * Coefficients are RBJ Audio EQ Cookbook, normalised by a0. All six EQ
 * types the EQ-8 offers are here: low-pass, high-pass, low shelf, high
 * shelf, bell (peaking) and notch. magnitudeDb() evaluates |H(e^jw)| from
 * the current coefficients, so a UI can draw the exact curve the audio
 * path applies, and tests can compare measured against formula.
 *
 * De-zippering: setTarget*() computes new coefficients into a target set;
 * process() moves the live set toward it by a fixed fraction per sample
 * (an exponential ramp, blueprint §3 step 2) so automation sweeps don't
 * click. snapToTarget() jumps (prepare, preset load).
 */
namespace loudio {

class BiquadD {
public:
    enum class Type { LowPass, HighPass, LowShelf, HighShelf, Bell, Notch };

    void prepare(double sampleRate, double rampSeconds = 0.005) {
        fs = sampleRate;
        /* Fraction per sample so that the ramp reaches ~99% in rampSeconds. */
        double samples = rampSeconds * sampleRate;
        lambda = samples > 1.0 ? 1.0 - std::exp(-4.6 / samples) : 1.0;
        reset();
    }

    void reset() { z1 = z2 = 0.0; }

    /* Target coefficients for the given design; takes effect through the ramp. */
    void setTarget(Type type, double freqHz, double q, double gainDb) {
        if (q < 0.025) q = 0.025;
        double nyquist = fs * 0.49;
        if (freqHz < 1.0) freqHz = 1.0;
        if (freqHz > nyquist) freqHz = nyquist;
        double A = std::pow(10.0, gainDb / 40.0);
        double w0 = 2.0 * M_PI * freqHz / fs;
        double cw = std::cos(w0), sw = std::sin(w0);
        double alpha = sw / (2.0 * q);
        double b0, b1, b2, a0, a1, a2;
        switch (type) {
            case Type::LowPass:
                b0 = (1.0 - cw) * 0.5; b1 = 1.0 - cw; b2 = b0;
                a0 = 1.0 + alpha; a1 = -2.0 * cw; a2 = 1.0 - alpha;
                break;
            case Type::HighPass:
                b0 = (1.0 + cw) * 0.5; b1 = -(1.0 + cw); b2 = b0;
                a0 = 1.0 + alpha; a1 = -2.0 * cw; a2 = 1.0 - alpha;
                break;
            case Type::Notch:
                b0 = 1.0; b1 = -2.0 * cw; b2 = 1.0;
                a0 = 1.0 + alpha; a1 = -2.0 * cw; a2 = 1.0 - alpha;
                break;
            case Type::Bell:
                b0 = 1.0 + alpha * A; b1 = -2.0 * cw; b2 = 1.0 - alpha * A;
                a0 = 1.0 + alpha / A; a1 = -2.0 * cw; a2 = 1.0 - alpha / A;
                break;
            case Type::LowShelf: {
                double sa = 2.0 * std::sqrt(A) * alpha;
                b0 = A * ((A + 1.0) - (A - 1.0) * cw + sa);
                b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cw);
                b2 = A * ((A + 1.0) - (A - 1.0) * cw - sa);
                a0 = (A + 1.0) + (A - 1.0) * cw + sa;
                a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cw);
                a2 = (A + 1.0) + (A - 1.0) * cw - sa;
                break;
            }
            case Type::HighShelf:
            default: {
                double sa = 2.0 * std::sqrt(A) * alpha;
                b0 = A * ((A + 1.0) + (A - 1.0) * cw + sa);
                b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw);
                b2 = A * ((A + 1.0) + (A - 1.0) * cw - sa);
                a0 = (A + 1.0) - (A - 1.0) * cw + sa;
                a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cw);
                a2 = (A + 1.0) - (A - 1.0) * cw - sa;
                break;
            }
        }
        tb0 = b0 / a0; tb1 = b1 / a0; tb2 = b2 / a0; ta1 = a1 / a0; ta2 = a2 / a0;
        if (!initialised) { snapToTarget(); initialised = true; }
    }

    /* Identity (0 dB everywhere): a bypassed band. */
    void setTargetBypass() {
        tb0 = 1.0; tb1 = tb2 = ta1 = ta2 = 0.0;
        if (!initialised) { snapToTarget(); initialised = true; }
    }

    void snapToTarget() { b0 = tb0; b1 = tb1; b2 = tb2; a1 = ta1; a2 = ta2; }

    /* TDF2, one sample; ramps the coefficients toward the target. */
    double process(double x) {
        if (b0 != tb0 || b1 != tb1 || b2 != tb2 || a1 != ta1 || a2 != ta2) {
            b0 += lambda * (tb0 - b0); b1 += lambda * (tb1 - b1); b2 += lambda * (tb2 - b2);
            a1 += lambda * (ta1 - a1); a2 += lambda * (ta2 - a2);
            if (std::abs(b0 - tb0) + std::abs(b1 - tb1) + std::abs(b2 - tb2) + std::abs(a1 - ta1) + std::abs(a2 - ta2) < 1e-12) snapToTarget();
        }
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    float processSample(float x) { return (float)process((double)x); }

    /* |H| in dB at freqHz for the TARGET coefficients (what the band will
     * sound like once the ramp settles; identical to the live ones when
     * not ramping). */
    double magnitudeDb(double freqHz) const {
        double w = 2.0 * M_PI * freqHz / fs;
        double c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2.0 * w), s2 = std::sin(2.0 * w);
        double nr = tb0 + tb1 * c1 + tb2 * c2, ni = -(tb1 * s1 + tb2 * s2);
        double dr = 1.0 + ta1 * c1 + ta2 * c2, di = -(ta1 * s1 + ta2 * s2);
        double mag2 = (nr * nr + ni * ni) / (dr * dr + di * di);
        return mag2 > 1e-30 ? 10.0 * std::log10(mag2) : -300.0;
    }

    double sampleRate() const { return fs; }

private:
    double fs = 48000.0;
    double lambda = 1.0;
    bool initialised = false;
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;      /* live */
    double tb0 = 1.0, tb1 = 0.0, tb2 = 0.0, ta1 = 0.0, ta2 = 0.0; /* target */
    double z1 = 0.0, z2 = 0.0;
};

} // namespace loudio
