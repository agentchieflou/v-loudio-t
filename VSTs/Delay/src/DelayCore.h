#pragma once

#include "DelayParams.h"
#include "loudio/dsp/FeedbackPath.h"
#include "loudio/dsp/Lfo.h"
#include "loudio/dsp/ModulatedDelay.h"
#include "loudio/dsp/SimpleBiquad.h"
#include "loudio/dsp/SimpleSmoothedValue.h"
#include "loudio/plugin/ParamQueue.h"
#include "loudio/plugin/Transport.h"

/*
 * Stereo / ping-pong delay (#142) on the platform core contract, after
 * Plugins/Delay/README.md. JUCE-free and LPI-free.
 *
 * Per sample:
 *   outL = delayL.read(timeL + mod),  outR = delayR.read(timeR + mod')
 *   fbL  = clip(filters(outL) * feedback), fbR likewise
 *   delayL.write(inL + fbL * (1 - cross) + fbR * cross)
 *   delayR.write(inR + fbR * (1 - cross) + fbL * cross)
 *   out  = dry + (wet - dry) * mix
 * The feedback is handled here rather than in ModulatedDelay so the
 * filters and the cross-feed sit inside the loop; ModulatedDelay's own
 * feedback stays at 0 and only its smoothed, modulatable read is used.
 * The soft clip (FeedbackPath, tanh) keeps feedback >= 100% bounded. A
 * low cut at 20 Hz or a high cut at 20 kHz (the range ends) is off, so
 * a clean loop repeats an impulse sample-exactly.
 *
 * Sync: with the parameter on, each side's time is its division at the
 * transport's tempo (last valid tempo, 120 BPM before any). Times glide
 * over 20 ms. No latency.
 */
class DelayCore {
public:
    DelayCore();

    void prepare(double sampleRate, int maxBlockSize);
    void process(float* left, float* right, int numSamples);

    static const loudio::ParamTable& table() { return kDelayParamTable; }
    int getParameterCount() const { return kDelayNumParams; }
    bool getParameterInfo(int index, loudio::ParamMeta* outMeta) const;
    float getParameterValue(int index) const;

    void setParameterValueDirect(int index, float value);
    bool setParameterValue(int index, float value);
    bool setParameterValues(const loudio::ParamUpdate* updates, size_t count);

    size_t getState(void* buffer, size_t bufferSize) const;
    bool setState(const void* data, size_t size);

    /* Host transport, once per block before process(). Only the tempo is
     * used (for synced divisions); PLAYING and ppq are ignored, so the
     * echoes never jump on a transport locate. */
    void setTransport(const loudio::TransportInfo& t);

    /* The times currently targeted, in ms (synced or free), for the UI. */
    float effectiveTimeMsL() const { return timeMsL; }
    float effectiveTimeMsR() const { return timeMsR; }
    double tempoBpm() const { return bpm; }

    /* Meter feeds, updated once per process(): block peak input and
     * output in dB. */
    float meterInputDb() const { return inputDb; }
    float meterOutputDb() const { return outputDb; }

private:
    void applyParams(bool immediate);
    void refreshTimes(bool immediate);

    double sampleRate = 44100.0;
    float params[kDelayNumParams];
    float applied[kDelayNumParams];
    loudio::ParamQueue<2048> paramQueue;

    loudio::ModulatedDelay delayL, delayR;
    SimpleBiquad lowCutL, lowCutR, highCutL, highCutR;
    loudio::FeedbackPath clipL, clipR;
    loudio::Lfo lfo;
    SimpleSmoothedValue smoothedFeedback, smoothedCross, smoothedMix, smoothedDepth; /* depth in samples */

    bool lowCutActive = true, highCutActive = true; /* range ends (20 Hz / 20 kHz) mean off */
    double bpm = 120.0;
    float timeMsL = 375.0f, timeMsR = 375.0f;
    float inputDb = -100.0f, outputDb = -100.0f;
};
