#pragma once

#include "GateParams.h"
#include "loudio/dsp/Hysteresis.h"
#include "loudio/dsp/LevelDetector.h"
#include "loudio/dsp/Lookahead.h"
#include "loudio/dsp/SidechainHpf.h"
#include "loudio/plugin/ParamQueue.h"

/*
 * Noise gate / downward expander (#138) on the platform core contract
 * (DSP/include/loudio/plugin/CORE_CONTRACT.md), after
 * Plugins/Dynamics/Gate/README.md. JUCE-free and LPI-free.
 *
 * Per sample, stereo-linked:
 *   sidechain = optional 2nd-order high-pass on a copy of the input;
 *   level     = peak detector (near-instant attack, 5 ms release) on
 *               max(|l|, |r|), in dB;
 *   state     = Hysteresis: open above threshold, close below
 *               threshold - hysteresis; closing starts the hold timer,
 *               re-opening restarts it;
 *   gain      = linear one-pole toward 1 (open, attack) or toward the
 *               range floor (closed after the hold, release);
 *   out       = input delayed by the lookahead, times gain.
 *
 * latencySamples() is the lookahead; both wrappers report it.
 */
class GateCore {
public:
    GateCore();

    void prepare(double sampleRate, int maxBlockSize);
    void process(float* left, float* right, int numSamples);

    static const loudio::ParamTable& table() { return kGateParamTable; }
    int getParameterCount() const { return kGateNumParams; }
    bool getParameterInfo(int index, loudio::ParamMeta* outMeta) const;
    float getParameterValue(int index) const;

    void setParameterValueDirect(int index, float value);
    bool setParameterValue(int index, float value);
    bool setParameterValues(const loudio::ParamUpdate* updates, size_t count);

    size_t getState(void* buffer, size_t bufferSize) const;
    bool setState(const void* data, size_t size);

    int latencySamples() const { return lookaheadL.latencySamples(); }

    /* Meter feeds for the editor, updated once per process() call: the
     * block's peak input level (dB), its deepest gain reduction (dB,
     * >= 0) and whether the gate was open at the block's end. Read on
     * the UI thread while the audio thread writes: torn reads are
     * cosmetic only. */
    float meterInputDb() const { return inputDb; }
    float meterGainReductionDb() const { return gainReductionDb; }
    bool meterOpen() const { return open; }

    /* The current linear gain (tests read the ballistics off it). */
    float currentGain() const { return gain; }

private:
    void applyParams();
    int lookaheadSamplesFor(float ms) const;

    double sampleRate = 44100.0;
    float params[kGateNumParams];
    float applied[kGateNumParams];
    loudio::ParamQueue<1024> paramQueue;

    loudio::SidechainHpf scHpfL, scHpfR;
    loudio::LevelDetector detector;
    loudio::Hysteresis hysteresis;
    loudio::Lookahead lookaheadL, lookaheadR;

    float aAttack = 0.0f, aRelease = 0.0f;
    float floorGain = 0.0f;
    int holdSamples = 0;
    int holdCounter = 0;
    bool open = false;
    float gain = 0.0f;

    float inputDb = -100.0f, gainReductionDb = 0.0f;
};
