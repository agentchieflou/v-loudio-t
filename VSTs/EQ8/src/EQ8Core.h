#pragma once

#include "EQ8Params.h"
#include "loudio/dsp/BiquadD.h"
#include "loudio/dsp/FFTAnalyzer.h"
#include "loudio/dsp/SimpleSmoothedValue.h"
#include "loudio/plugin/ParamQueue.h"

/*
 * EQ-8 DSP engine (#136) on the platform core contract: 8 cascaded
 * double-precision TDF2 biquads per channel (BiquadD, RBJ designs, 5 ms
 * coefficient ramps), a smoothed output gain, a bypass, and a post-EQ
 * spectrum analyzer (mono sum) for the editor. JUCE-free and LPI-free.
 *
 * Coefficients are only redesigned when a band's parameters change
 * (checked per block), so an idle EQ costs 16 biquads and nothing else.
 */
class EQ8Core {
public:
    EQ8Core();

    void prepare(double sampleRate, int maxBlockSize);
    void process(float* left, float* right, int numSamples);

    static const loudio::ParamTable& table() { return kEQ8ParamTable; }
    int getParameterCount() const { return kEQ8NumParams; }
    bool getParameterInfo(int index, loudio::ParamMeta* outMeta) const;
    float getParameterValue(int index) const;

    void setParameterValueDirect(int index, float value);
    bool setParameterValue(int index, float value);
    bool setParameterValues(const loudio::ParamUpdate* updates, size_t count);

    size_t getState(void* buffer, size_t bufferSize) const;
    bool setState(const void* data, size_t size);

    /* Post-EQ analyzer, 64 log-spaced magnitudes 0..1 (see FFTAnalyzer). */
    bool isAnalyzerFrameDirty() const { return analyzerDirty; }
    void getAnalyzerMagnitudes(float* out64);

private:
    void refreshDesigns(bool snap);

    double sampleRate = 44100.0;
    float params[kEQ8NumParams];
    float designed[kEQ8NumParams]; /* parameter values the current designs were made from */
    loudio::ParamQueue<2048> paramQueue;

    loudio::BiquadD bandsL[kEQ8Bands];
    loudio::BiquadD bandsR[kEQ8Bands];
    bool bandActive[kEQ8Bands] = {};
    SimpleSmoothedValue smoothedOutputGain; /* linear gain */

    FFTAnalyzer analyzer;
    float analyzerMagnitudes[64] = {};
    bool analyzerDirty = false;
};
