#pragma once

#include "LimiterParams.h"
#include "loudio/dsp/Lookahead.h"
#include "loudio/dsp/Oversampler.h"
#include "loudio/plugin/ParamQueue.h"

#include <vector>

/*
 * Lookahead brickwall / true-peak limiter (#139) on the platform core
 * contract, after Plugins/Dynamics/Limiter/README.md. JUCE-free and
 * LPI-free.
 *
 * The hard guarantee -- no output sample ever exceeds the ceiling, at
 * any block size -- comes from the structure, not from tuning:
 *
 *   required gain  g[n] = min(1, ceiling / peak[n])     (per channel)
 *   window minimum m[n] = min(g[n-L .. n])              (lookahead L)
 *   attack ramp    b[n] = mean(m[n-L+1 .. n])           (L-point box)
 *   release        r[n] = min(b[n], aR r[n-1] + (1-aR)) (rises toward 1)
 *   output         y[n] = x[n-L] r[n]
 *
 * Every m in the box covers index n-L, so b[n] <= g[n-L], r[n] <= b[n],
 * and |y[n]| <= ceiling. The box makes the gain reach its minimum over L
 * samples instead of stepping (the "attack"), the release lets it back up.
 * Stereo link mixes each channel's own g with min(gL, gR); both are
 * <= the channel's own g so the guarantee survives any link setting.
 *
 * True-peak: with oversampling on, peak[n] is the largest magnitude among
 * the 2 or 4 upsampled samples around input n (sample peaks are never
 * larger, so the guarantee still holds on the output samples).
 *
 * Latency is FIXED at the maximum lookahead plus the 4x upsampler's group
 * delay (10 ms + 24 samples), whatever the parameters say: lpi.latency.v1
 * promises a latency that does not change for the life of an activation.
 * The audio is always delayed by that; the detected peaks are delayed by
 * the remainder (maximum - lookahead - the mode's upsampler delay, 0 / 16
 * / 24) so the gain still arrives `lookahead` samples before the audio.
 */
class LimiterCore {
public:
    LimiterCore();

    void prepare(double sampleRate, int maxBlockSize);
    void process(float* left, float* right, int numSamples);

    static const loudio::ParamTable& table() { return kLimiterParamTable; }
    int getParameterCount() const { return kLimiterNumParams; }
    bool getParameterInfo(int index, loudio::ParamMeta* outMeta) const;
    float getParameterValue(int index) const;

    void setParameterValueDirect(int index, float value);
    bool setParameterValue(int index, float value);
    bool setParameterValues(const loudio::ParamUpdate* updates, size_t count);

    size_t getState(void* buffer, size_t bufferSize) const;
    bool setState(const void* data, size_t size);

    int latencySamples() const { return delayL.latencySamples(); }

    /* Upsampler group delay in base-rate samples, by oversampling mode;
     * the fixed latency is the maximum lookahead plus the 4x value. */
    static constexpr int detectorDelaySamples(int oversampling) {
        return oversampling == kLimiterOs2x ? loudio::HalfBandStage::kHalf / 2
             : oversampling == kLimiterOs4x ? loudio::HalfBandStage::kHalf / 2 + loudio::HalfBandStage::kHalf / 4
             : 0;
    }

    /* Meter feeds, updated once per process(): block peak input and
     * output (dB) and the deepest gain reduction (dB, >= 0). Torn reads
     * from the UI thread are cosmetic only. */
    float meterInputDb() const { return inputDb; }
    float meterOutputDb() const { return outputDb; }
    float meterGainReductionDb() const { return gainReductionDb; }

    /* Current release-stage gains (tests read the ballistics off them). */
    float currentGainL() const { return chains[0].release; }
    float currentGainR() const { return chains[1].release; }

private:
    /* One per channel: sliding-window minimum (monotonic queue), box sum,
     * release state. All rings are sized once in prepare() for the
     * maximum lookahead; a lookahead change only resets them. */
    struct Chain {
        std::vector<float> minValues;      /* ring of required gains, indexed by absolute sample % capacity */
        std::vector<long long> minDeque;   /* ring of absolute indices, values non-decreasing front to back */
        int dequeHead = 0, dequeCount = 0;
        long long written = 0;
        std::vector<float> boxValues;      /* ring of window minima, only the first boxLength slots cycle */
        int boxIndex = 0;
        double boxSum = 0.0;
        float release = 1.0f;

        void allocate(int capacity);
        void reset(int boxLength);
        float windowMin(float g, int window);
        float box(float m, int length);
    };

    void processChunk(float* left, float* right, int numSamples);
    void applyParams();
    int samplesFor(float ms) const;

    double sampleRate = 44100.0;
    int maxBlock = 512;
    float params[kLimiterNumParams];
    float applied[kLimiterNumParams];
    loudio::ParamQueue<1024> paramQueue;

    Chain chains[2];
    int window = 1;        /* L + 1 samples of minimum */
    int boxLength = 1;     /* max(1, L) */
    int maxWindow = 1;
    float aRelease = 0.0f;
    float ceilingGain = 1.0f;
    float inputGain = 1.0f;
    float link = 1.0f;
    int oversampling = kLimiterOsOff;

    loudio::Lookahead delayL, delayR;         /* audio, fixed */
    loudio::Lookahead peakDelayL, peakDelayR; /* detected peaks, remainder */
    loudio::Oversampler<2> os2L, os2R;
    loudio::Oversampler<4> os4L, os4R;

    float inputDb = -100.0f, outputDb = -100.0f, gainReductionDb = 0.0f;
};
