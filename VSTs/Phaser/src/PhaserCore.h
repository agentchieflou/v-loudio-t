#pragma once

#include "PhaserParams.h"
#include "loudio/dsp/AllPassStage.h"
#include "loudio/dsp/FeedbackPath.h"
#include "loudio/dsp/Lfo.h"
#include "loudio/dsp/SimpleSmoothedValue.h"
#include "loudio/plugin/ParamQueue.h"

/*
 * Phaser (#144) on the platform core contract, after
 * Plugins/Modulation/Phaser/README.md. JUCE-free and LPI-free.
 *
 * Per sample, per channel:
 *   corner = centre * 2^(kSweepOctaves * depth * lfo)
 *   x'     = in + clip(feedback * lastOut)          (one-sample feedback)
 *   wet    = N all-pass stages at `corner` applied to x'
 *   out    = in + (wet - in) * mix
 * The right channel reads the LFO stereoPhase degrees behind the left.
 * No latency.
 */
class PhaserCore {
public:
    PhaserCore();

    void prepare(double sampleRate, int maxBlockSize);
    void process(float* left, float* right, int numSamples);

    static const loudio::ParamTable& table() { return kPhaserParamTable; }
    int getParameterCount() const { return kPhaserNumParams; }
    bool getParameterInfo(int index, loudio::ParamMeta* outMeta) const;
    float getParameterValue(int index) const;

    void setParameterValueDirect(int index, float value);
    bool setParameterValue(int index, float value);
    bool setParameterValues(const loudio::ParamUpdate* updates, size_t count);

    size_t getState(void* buffer, size_t bufferSize) const;
    bool setState(const void* data, size_t size);

    int activeStages() const { return stages; }

private:
    struct Channel {
        loudio::AllPassStage allPass[kPhaserMaxStages];
        loudio::FeedbackPath feedback;
        float lastOut = 0.0f;
        void clear() { for (auto& s : allPass) s.clear(); lastOut = 0.0f; }
    };

    void applyParams(bool immediate);
    float processChannel(Channel& ch, float in, float cornerHz);

    double sampleRate = 44100.0;
    float params[kPhaserNumParams];
    float applied[kPhaserNumParams];
    loudio::ParamQueue<1024> paramQueue;

    Channel left, right;
    loudio::Lfo lfo;
    SimpleSmoothedValue smoothedDepth; /* 0..1 */
    SimpleSmoothedValue smoothedMix;   /* 0..1 */
    SimpleSmoothedValue smoothedCentre; /* Hz */
    int stages = 6;
    float phaseOffset = 0.25f;
};
