#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * Streaming TD-PSOLA pitch shifter (#146): time-domain pitch-synchronous
 * overlap-add. The caller feeds samples one at a time and, per hop,
 * tells it the current period in samples (0 = unvoiced) and the shift
 * ratio S (output pitch / input pitch). Internally:
 *
 *   analysis marks   a_k+1 = a_k + P            (input time)
 *   synthesis marks  s_m+1 = s_m + P / S        (input time too: the
 *                                               output is the input
 *                                               time line, delayed)
 *   for each s_m: take the analysis mark nearest it and overlap-add
 *   its 2P-long Hann-windowed segment at s_m; divide by the window sum
 *   so the level does not depend on S.
 *
 * Unvoiced frames use a fixed pseudo period with S = 1: Hann windows of
 * length 2P at hop P sum to 1, so the shifter is then an exact (to float)
 * passthrough. Formant shift resamples each segment by 2^(semitones/12)
 * around its mark (the segment keeps its length, its content is read
 * faster or slower), which moves the spectral envelope without changing
 * the pitch.
 *
 * Latency is fixed at latencySamples() = 2048 + max period: a synthesis
 * mark is processed once its segment is fully available and before its
 * start has been emitted. Everything is allocated in prepare().
 */
namespace loudio {

class PsolaShifter {
public:
    static constexpr int kPseudoPeriod = 256;

    void prepare(double sampleRate, int maxPeriodSamples) {
        fs = sampleRate;
        maxPeriod = maxPeriodSamples < kPseudoPeriod ? kPseudoPeriod : maxPeriodSamples;
        latency = 2048 + maxPeriod;
        capacity = 1;
        while (capacity < latency + 4 * maxPeriod + 16) capacity <<= 1;
        mask = capacity - 1;
        input.assign((size_t)capacity, 0.0f);
        acc.assign((size_t)capacity, 0.0f);
        wsum.assign((size_t)capacity, 0.0f);
        reset();
    }

    void reset() {
        std::fill(input.begin(), input.end(), 0.0f);
        std::fill(acc.begin(), acc.end(), 0.0f);
        std::fill(wsum.begin(), wsum.end(), 0.0f);
        now = 0;
        nextAnalysis = 0.0;
        prevAnalysis = -(double)kPseudoPeriod;
        nextSynthesis = 0.0;
        period = kPseudoPeriod;
        ratio = 1.0;
        formant = 1.0;
    }

    int latencySamples() const { return latency; }

    /* Per hop (or whenever they change): the input period in samples
     * (<= 0 for unvoiced) and the pitch ratio. */
    void setPeriodAndRatio(float periodSamples, float pitchRatio) {
        if (periodSamples > 0.0f) {
            period = periodSamples > (float)maxPeriod ? (float)maxPeriod : (periodSamples < 16.0f ? 16.0f : periodSamples);
            ratio = pitchRatio < 0.25f ? 0.25f : (pitchRatio > 4.0f ? 4.0f : pitchRatio);
        } else {
            period = (float)kPseudoPeriod;
            ratio = 1.0f;
        }
    }
    void setFormantRatio(float r) { formant = r < 0.5f ? 0.5f : (r > 2.0f ? 2.0f : r); }

    /* Push one input sample, get the output sample `latency` samples back. */
    float process(float x) {
        input[(size_t)(now & mask)] = x;
        /* Synthesis marks are due once their whole segment is in. */
        while (nextSynthesis + 2.0 * (double)maxPeriod <= (double)now) placeSegment();
        const long long t = now - latency;
        float out = 0.0f;
        if (t >= 0) {
            const size_t i = (size_t)(t & mask);
            out = wsum[i] > 1e-6f ? acc[i] / wsum[i] : 0.0f;
            acc[i] = 0.0f;
            wsum[i] = 0.0f;
        }
        ++now;
        return out;
    }

private:
    float readInput(double t) const {
        /* Linear interpolation; t is an absolute input time within the ring. */
        long long i0 = (long long)std::floor(t);
        float f = (float)(t - (double)i0);
        float a = input[(size_t)(i0 & mask)], b = input[(size_t)((i0 + 1) & mask)];
        return a + (b - a) * f;
    }

    void placeSegment() {
        const double s = nextSynthesis;
        const int P = (int)std::lround(period);
        /* Advance the analysis marks past s, keeping the previous one. The
         * marks are fractional (the period rarely is a whole number of
         * samples; rounding them would detune the output by the drift). */
        while (nextAnalysis < s) { prevAnalysis = nextAnalysis; nextAnalysis += (double)period; }
        const double a = (s - prevAnalysis) < (nextAnalysis - s) ? prevAnalysis : nextAnalysis;
        /* Overlap-add the 2P Hann segment around a at s (rounded). */
        const long long sInt = (long long)std::lround(s);
        const long long emitted = now - latency; /* slots at or before this are out already */
        const float invP = 1.0f / (float)P;
        for (int j = -P; j <= P; ++j) {
            if (sInt + j <= emitted || sInt + j < 0) continue; /* never touch an emitted or pre-start slot */
            const float w = 0.5f + 0.5f * std::cos((float)M_PI * (float)j * invP);
            const double src = a + (double)j * (double)formant;
            const size_t idx = (size_t)((sInt + j) & mask);
            acc[idx] += w * readInput(src);
            wsum[idx] += w;
        }
        nextSynthesis += (double)period / (double)ratio;
    }

    double fs = 48000.0;
    int maxPeriod = 1024, latency = 3072, capacity = 1, mask = 0;
    std::vector<float> input, acc, wsum;
    long long now = 0;
    double nextAnalysis = 0.0, prevAnalysis = 0.0;
    double nextSynthesis = 0.0;
    float period = kPseudoPeriod, ratio = 1.0f, formant = 1.0f;
};

} // namespace loudio
