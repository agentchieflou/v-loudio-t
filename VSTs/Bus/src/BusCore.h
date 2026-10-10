#pragma once

#include "BusParams.h"
#include "loudio/dsp/OnePoleLPF.h"
#include "loudio/dsp/SimpleSmoothedValue.h"
#include "loudio/plugin/ParamQueue.h"

/*
 * Bus: channel utility (#141) on the platform core contract. JUCE-free
 * and LPI-free. Per sample, in this order:
 *
 *   swap L/R -> phase invert per channel -> mono sum -> width (mid/side,
 *   side scaled) -> pan (constant power: gL = sqrt2 cos(t), gR = sqrt2
 *   sin(t), t = (pan+1) pi/4, so centre is unity and a hard pan is
 *   +3 dB on the kept side, i.e. centre is -3 dB per side relative to
 *   hard) -> gain -> optional DC blocker (x - 5 Hz one-pole low-pass).
 *
 * Gain, pan gains and width ramp over 20 ms. With every parameter at its
 * default the core is a bit-exact passthrough: the width and pan stages
 * are skipped when their smoothed values sit exactly at neutral, and
 * the gain is exactly 1.0f.
 */
class BusCore {
public:
    BusCore();

    void prepare(double sampleRate, int maxBlockSize);
    void process(float* left, float* right, int numSamples);

    static const loudio::ParamTable& table() { return kBusParamTable; }
    int getParameterCount() const { return kBusNumParams; }
    bool getParameterInfo(int index, loudio::ParamMeta* outMeta) const;
    float getParameterValue(int index) const;

    void setParameterValueDirect(int index, float value);
    bool setParameterValue(int index, float value);
    bool setParameterValues(const loudio::ParamUpdate* updates, size_t count);

    size_t getState(void* buffer, size_t bufferSize) const;
    bool setState(const void* data, size_t size);

    /* Meter feeds, updated once per process(): block peak input and
     * output in dB (max of both channels). */
    float meterInputDb() const { return inputDb; }
    float meterOutputDb() const { return outputDb; }

    /* The pan law, for tests and the UI: gains for a pan in -1..1. */
    static void panGains(float pan, float& gainL, float& gainR);

private:
    void applyParams();

    double sampleRate = 44100.0;
    float params[kBusNumParams];
    float applied[kBusNumParams];
    loudio::ParamQueue<1024> paramQueue;

    SimpleSmoothedValue smoothedGain;  /* linear */
    SimpleSmoothedValue smoothedPanL, smoothedPanR;
    SimpleSmoothedValue smoothedWidth; /* 0..2 */
    OnePoleLPF dcL, dcR;
    float dcCoefficient = 0.999f;

    float inputDb = -100.0f, outputDb = -100.0f;
};
