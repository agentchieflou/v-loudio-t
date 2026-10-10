#include "EQ8Core.h"
#include "EQ8Response.h"
#include "loudio/dsp/Decibels.h"
#include "loudio/plugin/StateBlob.h"

#include <cstring>

namespace {
const char kStateMagic[4] = { 'E', 'Q', '8', '_' };
} // namespace

EQ8Core::EQ8Core() {
    kEQ8ParamTable.fillDefaults(params);
    for (int i = 0; i < kEQ8NumParams; ++i) designed[i] = params[i] - 1.0f; /* force the first design */
}

void EQ8Core::prepare(double newSampleRate, int /* maxBlockSize */) {
    sampleRate = newSampleRate;
    for (int b = 0; b < kEQ8Bands; ++b) {
        bandsL[b].prepare(sampleRate);
        bandsR[b].prepare(sampleRate);
    }
    smoothedOutputGain.reset(sampleRate, 0.02);
    smoothedOutputGain.setCurrentAndTargetValue(loudio::dbToGain(params[kEQ8OutputGain]));
    analyzer.prepare(sampleRate);
    analyzerDirty = false;
    refreshDesigns(true);
}

void EQ8Core::refreshDesigns(bool snap) {
    for (int b = 0; b < kEQ8Bands; ++b) {
        int base = b * kEQ8ParamsPerBand;
        bool changed = snap;
        for (int k = 0; k < kEQ8ParamsPerBand; ++k) changed = changed || (params[base + k] != designed[base + k]);
        if (!changed) continue;
        for (int k = 0; k < kEQ8ParamsPerBand; ++k) designed[base + k] = params[base + k];
        int type = (int)params[base + kEQ8BandType];
        double hz = params[base + kEQ8BandFreq], gain = params[base + kEQ8BandGain], q = params[base + kEQ8BandQ];
        bandActive[b] = eq8ApplyBandDesign(bandsL[b], type, hz, gain, q);
        eq8ApplyBandDesign(bandsR[b], type, hz, gain, q);
        if (snap) { bandsL[b].snapToTarget(); bandsR[b].snapToTarget(); bandsL[b].reset(); bandsR[b].reset(); }
    }
}

void EQ8Core::process(float* left, float* right, int numSamples) {
    paramQueue.drainInto(kEQ8ParamTable, params);
    refreshDesigns(false);
    smoothedOutputGain.setTargetValue(loudio::dbToGain(params[kEQ8OutputGain]));
    const bool bypass = params[kEQ8Bypass] > 0.5f;

    for (int i = 0; i < numSamples; ++i) {
        double l = left[i], r = right[i];
        float g = smoothedOutputGain.getNextValue();
        if (!bypass) {
            for (int b = 0; b < kEQ8Bands; ++b) {
                if (!bandActive[b]) continue;
                l = bandsL[b].process(l);
                r = bandsR[b].process(r);
            }
            l *= g;
            r *= g;
        }
        left[i] = (float)l;
        right[i] = (float)r;
        if (analyzer.pushSample((left[i] + right[i]) * 0.5f)) {
            analyzer.getMagnitudes(analyzerMagnitudes);
            analyzerDirty = true;
        }
    }
}

bool EQ8Core::getParameterInfo(int index, loudio::ParamMeta* outMeta) const {
    if (!kEQ8ParamTable.valid(index) || !outMeta) return false;
    *outMeta = kEQ8ParamTable[index];
    return true;
}

float EQ8Core::getParameterValue(int index) const {
    return kEQ8ParamTable.valid(index) ? params[index] : 0.0f;
}

void EQ8Core::setParameterValueDirect(int index, float value) {
    if (kEQ8ParamTable.valid(index)) params[index] = value;
}

bool EQ8Core::setParameterValue(int index, float value) {
    return paramQueue.push(kEQ8ParamTable, index, value);
}

bool EQ8Core::setParameterValues(const loudio::ParamUpdate* updates, size_t count) {
    return paramQueue.pushAll(kEQ8ParamTable, updates, count);
}

size_t EQ8Core::getState(void* buffer, size_t bufferSize) const {
    return loudio::StateBlob::write(kEQ8ParamTable, kStateMagic, params, buffer, bufferSize);
}

bool EQ8Core::setState(const void* data, size_t size) {
    return loudio::StateBlob::read(kEQ8ParamTable, kStateMagic, data, size, params);
}

void EQ8Core::getAnalyzerMagnitudes(float* out64) {
    if (out64) std::memcpy(out64, analyzerMagnitudes, sizeof(analyzerMagnitudes));
    analyzerDirty = false;
}
