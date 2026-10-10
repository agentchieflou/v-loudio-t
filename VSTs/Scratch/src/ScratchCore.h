#pragma once

#include "ScratchParams.h"
#include "loudio/plugin/ParamQueue.h"
#include "loudio/plugin/Transport.h"

/*
 * Scratch's DSP engine on the platform core contract
 * (DSP/include/loudio/plugin/CORE_CONTRACT.md). JUCE-free and LPI-free:
 * both wrappers (VSTs/common for VST3, PluginABI/wrapper for LPI) are
 * generated around it.
 *
 * Scaffold state: a stereo pass-through times `gain`. Replace process()
 * (and prepare()) with the real DSP, extend ScratchParams.h, then
 * regenerate the golden impulse responses:  dsp_tests --regen-golden
 */
class ScratchCore {
public:
    ScratchCore();

    void prepare(double sampleRate, int maxBlockSize);
    void process(float* left, float* right, int numSamples);

    static const loudio::ParamTable& table() { return kScratchParamTable; }
    int getParameterCount() const { return kScratchNumParams; }
    bool getParameterInfo(int index, loudio::ParamMeta* outMeta) const;
    float getParameterValue(int index) const;

    void setParameterValueDirect(int index, float value);
    bool setParameterValue(int index, float value);
    bool setParameterValues(const loudio::ParamUpdate* updates, size_t count);

    size_t getState(void* buffer, size_t bufferSize) const;
    bool setState(const void* data, size_t size);

    /* Host transport, once per block before process() (lpi.transport.v1 /
     * JUCE playhead). Delete this if the plugin never needs tempo. */
    void setTransport(const loudio::TransportInfo& t) { transport = t; }

private:
    double sampleRate = 44100.0;
    float params[kScratchNumParams];
    loudio::TransportInfo transport;
    loudio::ParamQueue<1024> paramQueue;
};
