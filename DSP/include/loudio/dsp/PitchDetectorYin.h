#pragma once
#include <cmath>
#include <vector>

/*
 * YIN fundamental-frequency estimator (de Cheveigne & Kawahara 2002) for
 * the AutoTune (#146) and anything else that needs a pitch. Steps 1-5 of
 * the paper: squared difference function over an integration window W,
 * cumulative mean normalised difference, absolute threshold with the
 * following local minimum, parabolic interpolation of the lag. No
 * step 6 (best local estimate) and no decimation: the plugin runs it once
 * per hop, which keeps the cost at W * tauMax per hop.
 *
 * estimate() reads `window() + maxLag()` samples (oldest first) and
 * returns true when a periodic candidate was found: hz within the range,
 * and the normalised difference at the chosen lag (0 = perfectly
 * periodic; the voicing threshold is kThreshold). Buffers are allocated
 * in prepare(); estimate() allocates nothing.
 */
namespace loudio {

class PitchDetectorYin {
public:
    static constexpr float kThreshold = 0.15f;   /* absolute threshold on the CMNDF */
    static constexpr float kVoicedLimit = 0.35f; /* above this the frame is unvoiced */

    void prepare(double sampleRate, int windowSamples, float minHz, float maxHz) {
        fs = sampleRate;
        W = windowSamples < 64 ? 64 : windowSamples;
        tauMin = (int)std::floor(fs / (double)maxHz);
        tauMax = (int)std::ceil(fs / (double)minHz);
        if (tauMin < 2) tauMin = 2;
        if (tauMax <= tauMin) tauMax = tauMin + 1;
        diff.assign((size_t)tauMax + 1, 0.0f);
        cmndf.assign((size_t)tauMax + 1, 1.0f);
    }

    /* Narrow the search range within what prepare() allocated (a voice
     * type switch); no allocation. */
    void setRange(float minHz, float maxHz) {
        int lo = (int)std::floor(fs / (double)maxHz), hi = (int)std::ceil(fs / (double)minHz);
        if (lo < 2) lo = 2;
        if (hi > (int)diff.size() - 1) hi = (int)diff.size() - 1;
        if (hi <= lo) hi = lo + 1;
        tauMin = lo;
        tauMax = hi;
    }

    int window() const { return W; }
    int maxLag() const { return tauMax; }
    int minLag() const { return tauMin; }
    /* Samples estimate() reads. */
    int requiredSamples() const { return W + tauMax; }

    bool estimate(const float* x, float& hz, float& aperiodicity) {
        /* Step 1-2: difference function d(tau), tau in [1, tauMax]. */
        for (int tau = 1; tau <= tauMax; ++tau) {
            double acc = 0.0;
            const float* a = x;
            const float* b = x + tau;
            for (int j = 0; j < W; ++j) { float e = a[j] - b[j]; acc += (double)e * e; }
            diff[(size_t)tau] = (float)acc;
        }
        /* Step 3: cumulative mean normalised difference. */
        double running = 0.0;
        cmndf[0] = 1.0f;
        for (int tau = 1; tau <= tauMax; ++tau) {
            running += diff[(size_t)tau];
            cmndf[(size_t)tau] = running > 0.0 ? (float)(diff[(size_t)tau] * (double)tau / running) : 1.0f;
        }
        /* Step 4: first lag under the threshold, then follow it to the
         * local minimum; fall back to the global minimum in range. */
        int best = -1;
        for (int tau = tauMin; tau <= tauMax; ++tau) {
            if (cmndf[(size_t)tau] < kThreshold) {
                while (tau + 1 <= tauMax && cmndf[(size_t)tau + 1] < cmndf[(size_t)tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0) {
            float minVal = 1e9f;
            for (int tau = tauMin; tau <= tauMax; ++tau) if (cmndf[(size_t)tau] < minVal) { minVal = cmndf[(size_t)tau]; best = tau; }
        }
        aperiodicity = cmndf[(size_t)best];
        /* Step 5: parabolic interpolation around the minimum. */
        double lag = (double)best;
        if (best > 1 && best < tauMax) {
            double s0 = cmndf[(size_t)best - 1], s1 = cmndf[(size_t)best], s2 = cmndf[(size_t)best + 1];
            double denom = s0 - 2.0 * s1 + s2;
            if (std::abs(denom) > 1e-12) lag += 0.5 * (s0 - s2) / denom;
        }
        hz = (float)(fs / lag);
        return aperiodicity < kVoicedLimit;
    }

private:
    double fs = 48000.0;
    int W = 1024, tauMin = 2, tauMax = 3;
    std::vector<float> diff, cmndf;
};

} // namespace loudio
