#pragma once

#include "GlueParams.h"
#include "loudio/dsp/Lookahead.h"
#include "loudio/dsp/Oversampler.h"
#include "loudio/dsp/SidechainHpf.h"
#include "loudio/dsp/SimpleSmoothedValue.h"
#include "loudio/plugin/ParamQueue.h"

#include <vector>

/*
 * "The Glue": VCA bus compressor, feedback topology (#140), after
 * Plugins/Compressors/Glue/README.md as amended by the platform spike
 * (§5.2): one-sample-delayed feedback loop, tanh VCA, our own 4x
 * oversampler (no WDF, no Eigen, no JUCE DSP). JUCE-free and LPI-free.
 *
 * The whole loop runs at 4x the sample rate, per sample, stereo-linked:
 *
 *   v        = x * g                          VCA (g from the previous sample)
 *   y        = Vmax * tanh(v / Vmax)          VCA saturation (Vmax = 4: 3rd
 *                                             harmonic ~ -46 dB at 0 dBFS,
 *                                             -66 dB at -10 dBFS)
 *   sidechain: y -> optional HPF -> |.| linked -> dB
 *   target   = (R - 1) * max(0, level - T)    feedback gain computer: with
 *                                             the OUTPUT level driving it,
 *                                             the in->out slope is 1/R
 *   timing   = SSL-style: attack one-pole into the gain reduction; release
 *              one-pole (switch), or Auto = max of a fast (100 ms) follower
 *              and a slow one (300 ms attack, 1.2 s release) so short
 *              transients recover quickly and sustained reduction lets go
 *              slowly (the hardware's two-capacitor network)
 *   g        = 10^(-gr/20)                    for the NEXT sample
 *   out      = (dry delayed by the oversampler) * (1 - mix)
 *            + downsample(y) * makeup * mix
 *
 * The one-sample delay in the loop is a quarter of a base-rate sample at
 * 4x; the tanh's harmonics land below 4 * fs / 2 and the half-band
 * downsampler removes what would alias. Latency = the oversampler's round
 * trip (48 samples), fixed.
 */
class GlueCore {
public:
    GlueCore();

    void prepare(double sampleRate, int maxBlockSize);
    void process(float* left, float* right, int numSamples);

    static const loudio::ParamTable& table() { return kGlueParamTable; }
    int getParameterCount() const { return kGlueNumParams; }
    bool getParameterInfo(int index, loudio::ParamMeta* outMeta) const;
    float getParameterValue(int index) const;

    void setParameterValueDirect(int index, float value);
    bool setParameterValue(int index, float value);
    bool setParameterValues(const loudio::ParamUpdate* updates, size_t count);

    size_t getState(void* buffer, size_t bufferSize) const;
    bool setState(const void* data, size_t size);

    static constexpr int kOversampling = 4;
    static constexpr float kVcaVmax = 4.0f;
    int latencySamples() const { return loudio::Oversampler<kOversampling>::latencySamples(); }

    /* Meter feeds, updated once per process(): block peak input/output
     * (dB) and the deepest gain reduction (dB, >= 0). Torn reads from the
     * UI thread are cosmetic only. */
    float meterInputDb() const { return inputDb; }
    float meterOutputDb() const { return outputDb; }
    float meterGainReductionDb() const { return gainReductionDb; }

    /* The gain reduction currently applied, in dB (tests read the timing). */
    float currentGainReductionDb() const { return grApplied; }

private:
    void processChunk(float* left, float* right, int numSamples);
    void applyParams();

    double sampleRate = 44100.0;
    double loopRate = 176400.0;
    int maxBlock = 512;
    float params[kGlueNumParams];
    float applied[kGlueNumParams];
    loudio::ParamQueue<1024> paramQueue;

    loudio::Oversampler<kOversampling> osL, osR;
    loudio::SidechainHpf scHpfL, scHpfR;
    loudio::Lookahead dryL, dryR;
    std::vector<float> dryBufL, dryBufR; /* the delayed dry signal for the mix, per chunk */
    SimpleSmoothedValue smoothedMakeup; /* linear */
    SimpleSmoothedValue smoothedMix;    /* 0..1 */

    float threshold = -10.0f, slope = 3.0f; /* R - 1 */
    float aAttack = 0.0f, aRelease = 0.0f;
    bool autoRelease = true;
    float aAutoFastRelease = 0.0f, aAutoSlowAttack = 0.0f, aAutoSlowRelease = 0.0f;
    float grFast = 0.0f, grSlow = 0.0f, grApplied = 0.0f;
    float gain = 1.0f; /* the VCA gain for the next loop sample */

    float inputDb = -100.0f, outputDb = -100.0f, gainReductionDb = 0.0f;
};
