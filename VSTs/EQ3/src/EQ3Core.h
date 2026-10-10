#pragma once

#include "EQ3Params.h"
#include "loudio/dsp/CrossoverLR4.h"
#include "loudio/dsp/SimpleSmoothedValue.h"
#include "loudio/plugin/ParamQueue.h"

/*
 * EQ-3: 3-band isolator with kill switches (#145) on the platform core
 * contract, after Plugins/EQ/EQ-3/README.md. JUCE-free and LPI-free.
 *
 * Per sample, per channel: ThreeBandCrossoverLR4 splits the input into
 * low / mid / high (phase-coherent, summing to an all-pass), each band is
 * scaled by its gain (0 when killed or at -60 dB) and the three are
 * summed. Gains ramp over 20 ms; the crossovers ramp inside the biquads
 * (5 ms). No latency.
 */
class EQ3Core {
public:
    EQ3Core();

    void prepare(double sampleRate, int maxBlockSize);
    void process(float* left, float* right, int numSamples);

    static const loudio::ParamTable& table() { return kEQ3ParamTable; }
    int getParameterCount() const { return kEQ3NumParams; }
    bool getParameterInfo(int index, loudio::ParamMeta* outMeta) const;
    float getParameterValue(int index) const;

    void setParameterValueDirect(int index, float value);
    bool setParameterValue(int index, float value);
    bool setParameterValues(const loudio::ParamUpdate* updates, size_t count);

    size_t getState(void* buffer, size_t bufferSize) const;
    bool setState(const void* data, size_t size);

    /* The linear gain a band is heading to (0 = killed), for tests/UI. */
    float targetBandGain(int band) const { return targets[band]; }

private:
    void applyParams(bool immediate);

    double sampleRate = 44100.0;
    float params[kEQ3NumParams];
    float applied[kEQ3NumParams];
    loudio::ParamQueue<1024> paramQueue;

    loudio::ThreeBandCrossoverLR4 splitL, splitR;
    SimpleSmoothedValue gains[3];
    float targets[3] = { 1.0f, 1.0f, 1.0f };
};
