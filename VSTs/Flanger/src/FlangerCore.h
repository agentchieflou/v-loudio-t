#pragma once

#include "FlangerParams.h"
#include "loudio/dsp/Lfo.h"
#include "loudio/dsp/ModulatedDelay.h"
#include "loudio/dsp/SimpleSmoothedValue.h"
#include "loudio/plugin/ParamQueue.h"

/*
 * Flanger (#143) on the platform core contract, after
 * Plugins/Modulation/Flanger/README.md. JUCE-free and LPI-free.
 *
 * Per sample, per channel:
 *   wet  = line.process(in, centreSamples * depth * lfo)   (line feeds back
 *          its own output through FeedbackPath, +/-, tanh-clipped)
 *   dry' = throughZero ? dryLine.process(in) (delayed by centre) : in
 *   out  = dry' + (wet - dry') * mix
 * The right channel's LFO value is read at phase + stereoPhase/360.
 * Depth and mix ramp over 20 ms; the centre ramps inside ModulatedDelay.
 * No reported latency (through-zero's centre delay is part of the effect).
 */
class FlangerCore {
public:
    FlangerCore();

    void prepare(double sampleRate, int maxBlockSize);
    void process(float* left, float* right, int numSamples);

    static const loudio::ParamTable& table() { return kFlangerParamTable; }
    int getParameterCount() const { return kFlangerNumParams; }
    bool getParameterInfo(int index, loudio::ParamMeta* outMeta) const;
    float getParameterValue(int index) const;

    void setParameterValueDirect(int index, float value);
    bool setParameterValue(int index, float value);
    bool setParameterValues(const loudio::ParamUpdate* updates, size_t count);

    size_t getState(void* buffer, size_t bufferSize) const;
    bool setState(const void* data, size_t size);

    /* The LFO's current phase in cycles, for the UI's sweep display. */
    float lfoPhase() const { return lfo.phaseCycles(); }

private:
    void applyParams(bool immediate);

    double sampleRate = 44100.0;
    float params[kFlangerNumParams];
    float applied[kFlangerNumParams];
    loudio::ParamQueue<1024> paramQueue;

    loudio::ModulatedDelay lineL, lineR, dryL, dryR;
    loudio::Lfo lfo;
    SimpleSmoothedValue smoothedDepth; /* 0..1 */
    SimpleSmoothedValue smoothedMix;   /* 0..1 */
    float centreSamples = 1.0f;
    float phaseOffset = 0.25f;
    bool throughZero = false;
};
