#include "FlangerCore.h"
#include "loudio/plugin/StateBlob.h"

#include <algorithm>
#include <cmath>

namespace {
const char kStateMagic[4] = { 'F', 'L', 'N', 'G' };
} // namespace

FlangerCore::FlangerCore() {
    kFlangerParamTable.fillDefaults(params);
    for (int i = 0; i < kFlangerNumParams; ++i) applied[i] = params[i] - 1.0f;
}

void FlangerCore::prepare(double newSampleRate, int /* maxBlockSize */) {
    sampleRate = newSampleRate;
    /* The modulated line sweeps up to 2 x centre; a little margin on top. */
    lineL.prepare(sampleRate, 2.0f * kFlangerMaxCentreMs + 1.0f);
    lineR.prepare(sampleRate, 2.0f * kFlangerMaxCentreMs + 1.0f);
    dryL.prepare(sampleRate, kFlangerMaxCentreMs + 1.0f);
    dryR.prepare(sampleRate, kFlangerMaxCentreMs + 1.0f);
    dryL.setFeedback(0.0f);
    dryR.setFeedback(0.0f);
    lfo.setShape(loudio::Lfo::Shape::Sine);
    lfo.prepare(sampleRate);
    smoothedDepth.reset(sampleRate, 0.02);
    smoothedMix.reset(sampleRate, 0.02);
    for (int i = 0; i < kFlangerNumParams; ++i) applied[i] = params[i] - 1.0f;
    applyParams(true);
}

void FlangerCore::applyParams(bool immediate) {
    bool changed = immediate;
    for (int i = 0; i < kFlangerNumParams && !changed; ++i) changed = params[i] != applied[i];
    if (!changed) return;

    const float centreMs = std::clamp(params[kFlangerCentre], 0.1f, kFlangerMaxCentreMs);
    centreSamples = (float)(centreMs * 0.001 * sampleRate);
    if (immediate) {
        lineL.setDelayMsImmediate(centreMs); lineR.setDelayMsImmediate(centreMs);
        dryL.setDelayMsImmediate(centreMs); dryR.setDelayMsImmediate(centreMs);
    } else {
        lineL.setDelayMs(centreMs); lineR.setDelayMs(centreMs);
        dryL.setDelayMs(centreMs); dryR.setDelayMs(centreMs);
    }
    const float fb = std::clamp(params[kFlangerFeedback], -95.0f, 95.0f) * 0.01f;
    lineL.setFeedback(fb);
    lineR.setFeedback(fb);
    lfo.setFrequencyHz(params[kFlangerRate]);
    phaseOffset = std::clamp(params[kFlangerStereoPhase], 0.0f, 180.0f) / 360.0f;
    throughZero = params[kFlangerThroughZero] > 0.5f;
    const float depth = std::clamp(params[kFlangerDepth], 0.0f, 100.0f) * 0.01f;
    const float mix = std::clamp(params[kFlangerMix], 0.0f, 100.0f) * 0.01f;
    if (immediate) { smoothedDepth.setCurrentAndTargetValue(depth); smoothedMix.setCurrentAndTargetValue(mix); }
    else { smoothedDepth.setTargetValue(depth); smoothedMix.setTargetValue(mix); }
    for (int i = 0; i < kFlangerNumParams; ++i) applied[i] = params[i];
}

void FlangerCore::process(float* left, float* right, int numSamples) {
    paramQueue.drainInto(kFlangerParamTable, params);
    applyParams(false);

    for (int i = 0; i < numSamples; ++i) {
        const float inL = left[i], inR = right[i];
        const float depth = smoothedDepth.getNextValue() * centreSamples;
        const float phase = lfo.phaseCycles();
        const float modL = depth * lfo.process();
        const float modR = depth * lfo.valueAt(phase + phaseOffset);

        const float wetL = lineL.process(inL, modL);
        const float wetR = lineR.process(inR, modR);
        float dl = inL, dr = inR;
        if (throughZero) { dl = dryL.process(inL); dr = dryR.process(inR); }
        else { dryL.process(inL); dryR.process(inR); } /* keep the lines primed for a click-free switch */

        const float mix = smoothedMix.getNextValue();
        left[i] = dl + (wetL - dl) * mix;
        right[i] = dr + (wetR - dr) * mix;
    }
}

bool FlangerCore::getParameterInfo(int index, loudio::ParamMeta* outMeta) const {
    if (!kFlangerParamTable.valid(index) || !outMeta) return false;
    *outMeta = kFlangerParamTable[index];
    return true;
}

float FlangerCore::getParameterValue(int index) const {
    return kFlangerParamTable.valid(index) ? params[index] : 0.0f;
}

void FlangerCore::setParameterValueDirect(int index, float value) {
    if (kFlangerParamTable.valid(index)) params[index] = value;
}

bool FlangerCore::setParameterValue(int index, float value) {
    return paramQueue.push(kFlangerParamTable, index, value);
}

bool FlangerCore::setParameterValues(const loudio::ParamUpdate* updates, size_t count) {
    return paramQueue.pushAll(kFlangerParamTable, updates, count);
}

size_t FlangerCore::getState(void* buffer, size_t bufferSize) const {
    return loudio::StateBlob::write(kFlangerParamTable, kStateMagic, params, buffer, bufferSize);
}

bool FlangerCore::setState(const void* data, size_t size) {
    return loudio::StateBlob::read(kFlangerParamTable, kStateMagic, data, size, params);
}
