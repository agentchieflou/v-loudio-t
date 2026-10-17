#include "DelayCore.h"
#include "loudio/dsp/Decibels.h"
#include "loudio/plugin/StateBlob.h"

#include <algorithm>
#include <cmath>

namespace {
const char kStateMagic[4] = { 'D', 'L', 'A', 'Y' };
} // namespace

DelayCore::DelayCore() {
    kDelayParamTable.fillDefaults(params);
    for (int i = 0; i < kDelayNumParams; ++i) applied[i] = params[i] - 1.0f;
}

void DelayCore::prepare(double newSampleRate, int /* maxBlockSize */) {
    sampleRate = newSampleRate;
    delayL.prepare(sampleRate, kDelayMaxTimeMs + kDelayMaxModDepthMs + 1.0f);
    delayR.prepare(sampleRate, kDelayMaxTimeMs + kDelayMaxModDepthMs + 1.0f);
    delayL.setFeedback(0.0f);
    delayR.setFeedback(0.0f);
    lowCutL.reset(); lowCutR.reset(); highCutL.reset(); highCutR.reset();
    clipL.setGain(1.0f);
    clipR.setGain(1.0f);
    lfo.setShape(loudio::Lfo::Shape::Sine);
    lfo.prepare(sampleRate);
    smoothedFeedback.reset(sampleRate, 0.02);
    smoothedCross.reset(sampleRate, 0.02);
    smoothedMix.reset(sampleRate, 0.02);
    smoothedDepth.reset(sampleRate, 0.02);
    inputDb = outputDb = -100.0f;
    for (int i = 0; i < kDelayNumParams; ++i) applied[i] = params[i] - 1.0f;
    applyParams(true);
}

void DelayCore::setTransport(const loudio::TransportInfo& t) {
    if (t.tempoValid() && t.tempoBpm > 0.0 && t.tempoBpm != bpm) {
        bpm = t.tempoBpm;
        if (params[kDelaySync] > 0.5f) refreshTimes(false);
    }
}

void DelayCore::refreshTimes(bool immediate) {
    if (params[kDelaySync] > 0.5f) {
        int dl = std::clamp((int)params[kDelayDivisionL], 0, kDelayNumDivisions - 1);
        int dr = std::clamp((int)params[kDelayDivisionR], 0, kDelayNumDivisions - 1);
        timeMsL = loudio::tempoDivisionSeconds(bpm, kDelayDivisionBeats[dl]) * 1000.0f;
        timeMsR = loudio::tempoDivisionSeconds(bpm, kDelayDivisionBeats[dr]) * 1000.0f;
    } else {
        timeMsL = params[kDelayTimeL];
        timeMsR = params[kDelayTimeR];
    }
    timeMsL = std::clamp(timeMsL, 1.0f, kDelayMaxTimeMs);
    timeMsR = std::clamp(timeMsR, 1.0f, kDelayMaxTimeMs);
    if (immediate) { delayL.setDelayMsImmediate(timeMsL); delayR.setDelayMsImmediate(timeMsR); }
    else { delayL.setDelayMs(timeMsL); delayR.setDelayMs(timeMsR); }
}

void DelayCore::applyParams(bool immediate) {
    bool changed = immediate;
    for (int i = 0; i < kDelayNumParams && !changed; ++i) changed = params[i] != applied[i];
    if (!changed) return;

    refreshTimes(immediate);
    /* Floor first, then the Nyquist limit: same as std::clamp at any real
     * rate, and still defined below ~2.2 kHz, where the limit drops under
     * the floor (std::clamp's hi < lo is undefined; the VST3 validator
     * runs at 1234.5678 Hz, #171). */
    const float nyquistLimit = (float)sampleRate * 0.45f;
    float lowHz = std::min(std::max(params[kDelayLowCut], 20.0f), nyquistLimit);
    float highHz = std::min(std::max(params[kDelayHighCut], 1000.0f), nyquistLimit);
    lowCutActive = params[kDelayLowCut] > 20.0f;
    highCutActive = params[kDelayHighCut] < 20000.0f;
    if (immediate || params[kDelayLowCut] != applied[kDelayLowCut]) {
        lowCutL.makeHighPass(sampleRate, lowHz, 0.70710678f);
        lowCutR.makeHighPass(sampleRate, lowHz, 0.70710678f);
    }
    if (immediate || params[kDelayHighCut] != applied[kDelayHighCut]) {
        highCutL.makeLowPass(sampleRate, highHz, 0.70710678f);
        highCutR.makeLowPass(sampleRate, highHz, 0.70710678f);
    }
    lfo.setFrequencyHz(params[kDelayModRate]);
    const float fb = std::clamp(params[kDelayFeedback], 0.0f, 110.0f) * 0.01f;
    const float cross = std::clamp(params[kDelayCrossFeed], 0.0f, 100.0f) * 0.01f;
    const float mix = std::clamp(params[kDelayMix], 0.0f, 100.0f) * 0.01f;
    const float depth = (float)(std::clamp(params[kDelayModDepth], 0.0f, kDelayMaxModDepthMs) * 0.001 * sampleRate);
    if (immediate) {
        smoothedFeedback.setCurrentAndTargetValue(fb);
        smoothedCross.setCurrentAndTargetValue(cross);
        smoothedMix.setCurrentAndTargetValue(mix);
        smoothedDepth.setCurrentAndTargetValue(depth);
    } else {
        smoothedFeedback.setTargetValue(fb);
        smoothedCross.setTargetValue(cross);
        smoothedMix.setTargetValue(mix);
        smoothedDepth.setTargetValue(depth);
    }
    for (int i = 0; i < kDelayNumParams; ++i) applied[i] = params[i];
}

void DelayCore::process(float* left, float* right, int numSamples) {
    paramQueue.drainInto(kDelayParamTable, params);
    applyParams(false);
    float inPeak = 0.0f, outPeak = 0.0f;

    for (int i = 0; i < numSamples; ++i) {
        const float inL = left[i], inR = right[i];
        inPeak = std::max(inPeak, std::max(std::abs(inL), std::abs(inR)));

        const float depth = smoothedDepth.getNextValue();
        const float phase = lfo.phaseCycles();
        const float modL = depth * lfo.process();
        const float modR = depth * lfo.valueAt(phase + 0.25f);

        /* Taps first, then this sample's filtered, clipped feedback, then
         * the writes (own feedback plus the other side's cross-feed). */
        const float outL = delayL.read(modL);
        const float outR = delayR.read(modR);
        const float fb = smoothedFeedback.getNextValue();
        const float cross = smoothedCross.getNextValue();
        float fl = outL, fr = outR;
        if (lowCutActive) { fl = lowCutL.processSample(fl); fr = lowCutR.processSample(fr); }
        if (highCutActive) { fl = highCutL.processSample(fl); fr = highCutR.processSample(fr); }
        const float fbL = clipL.process(fl * fb);
        const float fbR = clipR.process(fr * fb);
        delayL.write(inL + fbL + (fbR - fbL) * cross);
        delayR.write(inR + fbR + (fbL - fbR) * cross);

        const float mix = smoothedMix.getNextValue();
        left[i] = inL + (outL - inL) * mix;
        right[i] = inR + (outR - inR) * mix;
        outPeak = std::max(outPeak, std::max(std::abs(left[i]), std::abs(right[i])));
    }
    inputDb = loudio::gainToDb(inPeak);
    outputDb = loudio::gainToDb(outPeak);
}

bool DelayCore::getParameterInfo(int index, loudio::ParamMeta* outMeta) const {
    if (!kDelayParamTable.valid(index) || !outMeta) return false;
    *outMeta = kDelayParamTable[index];
    return true;
}

float DelayCore::getParameterValue(int index) const {
    return kDelayParamTable.valid(index) ? params[index] : 0.0f;
}

void DelayCore::setParameterValueDirect(int index, float value) {
    if (kDelayParamTable.valid(index)) params[index] = value;
}

bool DelayCore::setParameterValue(int index, float value) {
    return paramQueue.push(kDelayParamTable, index, value);
}

bool DelayCore::setParameterValues(const loudio::ParamUpdate* updates, size_t count) {
    return paramQueue.pushAll(kDelayParamTable, updates, count);
}

size_t DelayCore::getState(void* buffer, size_t bufferSize) const {
    return loudio::StateBlob::write(kDelayParamTable, kStateMagic, params, buffer, bufferSize);
}

bool DelayCore::setState(const void* data, size_t size) {
    return loudio::StateBlob::read(kDelayParamTable, kStateMagic, data, size, params);
}
