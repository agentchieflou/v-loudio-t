#include "PhaserCore.h"
#include "loudio/plugin/StateBlob.h"

#include <algorithm>
#include <cmath>

namespace {
const char kStateMagic[4] = { 'P', 'H', 'S', 'R' };
} // namespace

PhaserCore::PhaserCore() {
    kPhaserParamTable.fillDefaults(params);
    for (int i = 0; i < kPhaserNumParams; ++i) applied[i] = params[i] - 1.0f;
}

void PhaserCore::prepare(double newSampleRate, int /* maxBlockSize */) {
    sampleRate = newSampleRate;
    left.clear();
    right.clear();
    lfo.setShape(loudio::Lfo::Shape::Sine);
    lfo.prepare(sampleRate);
    smoothedDepth.reset(sampleRate, 0.02);
    smoothedMix.reset(sampleRate, 0.02);
    smoothedCentre.reset(sampleRate, 0.05);
    for (int i = 0; i < kPhaserNumParams; ++i) applied[i] = params[i] - 1.0f;
    applyParams(true);
}

void PhaserCore::applyParams(bool immediate) {
    bool changed = immediate;
    for (int i = 0; i < kPhaserNumParams && !changed; ++i) changed = params[i] != applied[i];
    if (!changed) return;

    lfo.setFrequencyHz(params[kPhaserRate]);
    phaseOffset = std::clamp(params[kPhaserStereoPhase], 0.0f, 180.0f) / 360.0f;
    stages = kPhaserStageCounts[std::clamp((int)params[kPhaserStages], 0, kPhaserNumStageOptions - 1)];
    const float fb = std::clamp(params[kPhaserFeedback], -95.0f, 95.0f) * 0.01f;
    left.feedback.setGain(fb);
    right.feedback.setGain(fb);
    const float depth = std::clamp(params[kPhaserDepth], 0.0f, 100.0f) * 0.01f;
    const float mix = std::clamp(params[kPhaserMix], 0.0f, 100.0f) * 0.01f;
    const float centre = std::clamp(params[kPhaserCentre], 100.0f, 5000.0f);
    if (immediate) {
        smoothedDepth.setCurrentAndTargetValue(depth);
        smoothedMix.setCurrentAndTargetValue(mix);
        smoothedCentre.setCurrentAndTargetValue(centre);
    } else {
        smoothedDepth.setTargetValue(depth);
        smoothedMix.setTargetValue(mix);
        smoothedCentre.setTargetValue(centre);
    }
    for (int i = 0; i < kPhaserNumParams; ++i) applied[i] = params[i];
}

float PhaserCore::processChannel(Channel& ch, float in, float cornerHz) {
    float x = in + ch.feedback.process(ch.lastOut);
    for (int s = 0; s < stages; ++s) {
        ch.allPass[s].setCornerFrequency(sampleRate, cornerHz);
        x = ch.allPass[s].process(x);
    }
    ch.lastOut = x;
    return x;
}

void PhaserCore::process(float* leftBuf, float* rightBuf, int numSamples) {
    paramQueue.drainInto(kPhaserParamTable, params);
    applyParams(false);

    for (int i = 0; i < numSamples; ++i) {
        const float inL = leftBuf[i], inR = rightBuf[i];
        const float depth = smoothedDepth.getNextValue() * kPhaserSweepOctaves;
        const float centre = smoothedCentre.getNextValue();
        const float phase = lfo.phaseCycles();
        const float cornerL = centre * std::exp2(depth * lfo.process());
        const float cornerR = centre * std::exp2(depth * lfo.valueAt(phase + phaseOffset));

        const float wetL = processChannel(left, inL, cornerL);
        const float wetR = processChannel(right, inR, cornerR);
        const float mix = smoothedMix.getNextValue();
        leftBuf[i] = inL + (wetL - inL) * mix;
        rightBuf[i] = inR + (wetR - inR) * mix;
    }
}

bool PhaserCore::getParameterInfo(int index, loudio::ParamMeta* outMeta) const {
    if (!kPhaserParamTable.valid(index) || !outMeta) return false;
    *outMeta = kPhaserParamTable[index];
    return true;
}

float PhaserCore::getParameterValue(int index) const {
    return kPhaserParamTable.valid(index) ? params[index] : 0.0f;
}

void PhaserCore::setParameterValueDirect(int index, float value) {
    if (kPhaserParamTable.valid(index)) params[index] = value;
}

bool PhaserCore::setParameterValue(int index, float value) {
    return paramQueue.push(kPhaserParamTable, index, value);
}

bool PhaserCore::setParameterValues(const loudio::ParamUpdate* updates, size_t count) {
    return paramQueue.pushAll(kPhaserParamTable, updates, count);
}

size_t PhaserCore::getState(void* buffer, size_t bufferSize) const {
    return loudio::StateBlob::write(kPhaserParamTable, kStateMagic, params, buffer, bufferSize);
}

bool PhaserCore::setState(const void* data, size_t size) {
    return loudio::StateBlob::read(kPhaserParamTable, kStateMagic, data, size, params);
}
