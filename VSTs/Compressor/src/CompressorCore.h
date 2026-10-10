#pragma once

#include "CompressorParams.h"
#include "loudio/dsp/GainComputer.h"
#include "loudio/dsp/LevelDetector.h"
#include "loudio/dsp/Lookahead.h"
#include "loudio/dsp/SidechainHpf.h"
#include "loudio/dsp/SimpleSmoothedValue.h"
#include "loudio/plugin/ParamQueue.h"

/*
 * Feed-forward dynamic range compressor (#137) on the platform core
 * contract (DSP/include/loudio/plugin/CORE_CONTRACT.md), after the
 * blueprint in Plugins/Compressors/Compressor/README.md and Giannoulis,
 * Massberg & Reiss (2012). JUCE-free and LPI-free.
 *
 * Per sample, stereo-linked:
 *   sidechain = optional 2nd-order high-pass on a copy of the input;
 *   level     = Peak: max(|l|, |r|)  |  RMS: 20 ms one-pole mean square;
 *   target GR = GainComputer (soft knee) on the level in dB;
 *   smoothed  = "smooth decoupled" peak follower in the dB domain:
 *                 y1 = max(target, aR*y1 + (1-aR)*target)  (release)
 *                 y  = aA*y + (1-aA)*y1                     (attack)
 *               so a step in gain reduction reaches 63% in one attack
 *               time and decays to 37% in one release time;
 *   gain      = 10^(-y/20) * makeup, applied to the input delayed by the
 *               lookahead (the detector sees the undelayed signal);
 *   out       = dry * (1 - mix) + wet * mix, both on the delayed signal.
 *
 * latencySamples() is the lookahead; both wrappers report it. It follows
 * the parameter immediately (the delay line is sized for the maximum), so
 * a host that only reads latency on activation sees the value at that
 * moment -- the LPI contract -- while the JUCE wrapper re-reports changes.
 */
class CompressorCore {
public:
    CompressorCore();

    void prepare(double sampleRate, int maxBlockSize);
    void process(float* left, float* right, int numSamples);

    static const loudio::ParamTable& table() { return kCompressorParamTable; }
    int getParameterCount() const { return kCompressorNumParams; }
    bool getParameterInfo(int index, loudio::ParamMeta* outMeta) const;
    float getParameterValue(int index) const;

    void setParameterValueDirect(int index, float value);
    bool setParameterValue(int index, float value);
    bool setParameterValues(const loudio::ParamUpdate* updates, size_t count);

    size_t getState(void* buffer, size_t bufferSize) const;
    bool setState(const void* data, size_t size);

    int latencySamples() const { return lookaheadL.latencySamples(); }

    /* Meter feeds for the editor, updated once per process() call: the
     * block's peak input and output levels and its deepest gain
     * reduction, all in dB (levels floor at -100 dB; GR >= 0). Read on
     * the UI thread while the audio thread writes: torn reads are
     * cosmetic only. */
    float meterInputDb() const { return inputDb; }
    float meterOutputDb() const { return outputDb; }
    float meterGainReductionDb() const { return gainReductionDb; }

    /* The makeup actually applied (manual, or the auto value), in dB. */
    float effectiveMakeupDb() const;

private:
    void applyParams();
    int lookaheadSamplesFor(float ms) const;

    double sampleRate = 44100.0;
    float params[kCompressorNumParams];
    float applied[kCompressorNumParams]; /* values the DSP settings were last derived from */
    loudio::ParamQueue<1024> paramQueue;

    loudio::SidechainHpf scHpfL, scHpfR;
    loudio::LevelDetector rmsDetector;
    loudio::GainComputer computer;
    loudio::Lookahead lookaheadL, lookaheadR;
    SimpleSmoothedValue smoothedMakeup; /* linear */
    SimpleSmoothedValue smoothedMix;    /* 0..1 */

    float aAttack = 0.0f, aRelease = 0.0f;
    float grFast = 0.0f;     /* y1: release-only follower */
    float grSmoothed = 0.0f; /* y:  attack-smoothed, what the gain is made from */

    float inputDb = -100.0f, outputDb = -100.0f, gainReductionDb = 0.0f;
};
