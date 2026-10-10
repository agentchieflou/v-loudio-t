#pragma once
#include <cmath>
#include <vector>

/*
 * JUCE-free 2x / 4x oversampling for saturation stages (P8, #134): the
 * Limiter's true-peak detection, the Glue's tanh VCA, a Saturator.
 *
 * Design: cascaded 2x stages, each a 65-tap linear-phase half-band FIR
 * (Kaiser window, beta 9.5: stopband about -95 dB, round trip flat within
 * -80 dB to beyond 12 kHz at 48 kHz). Linear phase was chosen over
 * polyphase IIR: more latency (0.7 ms / 1 ms at 48 kHz), but no phase
 * shift, and the latency is an exact integer reported by latencySamples()
 * for lpi.latency.v1 / setLatencySamples:
 *
 *   2x: 32 base-rate samples     4x: 48 base-rate samples
 *
 * Usage per block:   float* up = os.upsample(in, n);
 *                    ... process up[0 .. n * Factor) in place ...
 *                    os.downsample(up, out, n);
 * Buffers are allocated in prepare(maxBlockSize); the per-block calls never
 * allocate. One Oversampler per channel.
 */
namespace loudio {

class HalfBandStage {
public:
    static constexpr int kTaps = 65;              /* 4m+1 so kHalf is even and the 4x latency is a whole base sample */
    static constexpr int kHalf = (kTaps - 1) / 2; /* 32: group delay in samples at the higher rate */

    HalfBandStage() { designTaps(); }

    /* maxInputSamples: the most samples upsample() will be given per call. */
    void prepare(int maxInputSamples) {
        upOut.assign((size_t)(maxInputSamples > 0 ? maxInputSamples : 1) * 2, 0.0f);
        clear();
    }

    void clear() {
        for (float& v : upHistory) v = 0.0f;
        for (float& v : downHistory) v = 0.0f;
    }

    /* n input samples -> 2n samples in the internal buffer (zero-stuff, then
     * the half-band FIR with gain 2 to restore the amplitude). */
    float* upsample(const float* in, int n) {
        for (int i = 0; i < n; ++i) {
            upOut[(size_t)(2 * i)] = 2.0f * filter(upHistory, in[i]);
            upOut[(size_t)(2 * i + 1)] = 2.0f * filter(upHistory, 0.0f);
        }
        return upOut.data();
    }

    /* 2n input samples -> n output samples: FIR at the higher rate, keeping
     * the output on the EVEN phase so the round-trip delay (kHalf up +
     * kHalf down, at the higher rate) lands on a whole base-rate sample. */
    void downsample(const float* in, float* out, int n) {
        for (int i = 0; i < n; ++i) {
            out[i] = filter(downHistory, in[2 * i]);
            filter(downHistory, in[2 * i + 1]);
        }
    }

    const float* coefficients() const { return taps; }

private:
    /* Pushes x into history (newest first) and returns the FIR output. */
    float filter(float* history, float x) {
        for (int k = kTaps - 1; k > 0; --k) history[k] = history[k - 1];
        history[0] = x;
        float acc = 0.0f;
        for (int k = 0; k < kTaps; ++k) acc += taps[k] * history[k];
        return acc;
    }

    static float besselI0(float x) {
        float sum = 1.0f, term = 1.0f;
        for (int k = 1; k < 60; ++k) {
            float h = x / (2.0f * (float)k);
            term *= h * h;
            sum += term;
            if (term < 1e-10f * sum) break;
        }
        return sum;
    }

    void designTaps() {
        const float pi = 3.14159265358979f;
        const float beta = 9.5f; /* ~ -95 dB stopband and passband ripple per pass, so the up+down round trip stays within -80 dB */
        const float denom = besselI0(beta);
        for (int k = 0; k < kTaps; ++k) {
            int m = k - kHalf;
            float sinc = (m == 0) ? 0.5f : std::sin(0.5f * pi * (float)m) / (pi * (float)m);
            float r = (float)m / (float)kHalf;
            float window = besselI0(beta * std::sqrt(1.0f - r * r)) / denom;
            taps[k] = sinc * window;
        }
        float sum = 0.0f;
        for (int k = 0; k < kTaps; ++k) sum += taps[k];
        for (int k = 0; k < kTaps; ++k) taps[k] /= sum; /* DC gain exactly 1 */
    }

    float taps[kTaps] = {};
    float upHistory[kTaps] = {};
    float downHistory[kTaps] = {};
    std::vector<float> upOut;
};

template <int Factor>
class Oversampler {
public:
    static_assert(Factor == 2 || Factor == 4, "Oversampler supports 2x and 4x");
    static constexpr int kStages = (Factor == 2) ? 1 : 2;

    void prepare(int maxBlockSize) {
        if (maxBlockSize < 1) maxBlockSize = 1;
        stages[0].prepare(maxBlockSize);
        if constexpr (kStages == 2) {
            stages[1].prepare(maxBlockSize * 2);
            mid.assign((size_t)maxBlockSize * 2, 0.0f);
        }
    }

    void clear() { for (auto& s : stages) s.clear(); }

    /* Round trip up -> down through every stage, in base-rate samples:
     * each 2x stage adds kHalf samples at its own higher rate on the way
     * up and again on the way down, i.e. kHalf base samples for the first
     * stage and kHalf/2 for the second. */
    static constexpr int latencySamples() {
        return (Factor == 2) ? HalfBandStage::kHalf : HalfBandStage::kHalf + HalfBandStage::kHalf / 2;
    }

    /* n samples in -> n * Factor samples (internal buffer, valid until the next call). */
    float* upsample(const float* in, int n) {
        float* buf = stages[0].upsample(in, n);
        if constexpr (kStages == 2) buf = stages[1].upsample(buf, n * 2);
        return buf;
    }

    /* n * Factor samples in -> n samples out. `in` may be the upsample() buffer. */
    void downsample(const float* in, float* out, int n) {
        if constexpr (kStages == 2) {
            stages[1].downsample(in, mid.data(), n * 2);
            stages[0].downsample(mid.data(), out, n);
        } else {
            stages[0].downsample(in, out, n);
        }
    }

private:
    HalfBandStage stages[kStages];
    std::vector<float> mid;
};

} // namespace loudio
