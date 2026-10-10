#include "EQ3Core.h"
#include "loudio/dsp/Decibels.h"
#include "loudio/plugin/StateBlob.h"

#include <algorithm>
#include <cmath>

namespace {
const char kStateMagic[4] = { 'E', 'Q', '3', '_' };
} // namespace

EQ3Core::EQ3Core() {
    kEQ3ParamTable.fillDefaults(params);
    for (int i = 0; i < kEQ3NumParams; ++i) applied[i] = params[i] - 1.0f;
}

void EQ3Core::prepare(double newSampleRate, int /* maxBlockSize */) {
    sampleRate = newSampleRate;
    splitL.prepare(sampleRate);
    splitR.prepare(sampleRate);
    for (auto& g : gains) g.reset(sampleRate, 0.02);
    for (int i = 0; i < kEQ3NumParams; ++i) applied[i] = params[i] - 1.0f;
    applyParams(true);
}

void EQ3Core::applyParams(bool immediate) {
    bool changed = immediate;
    for (int i = 0; i < kEQ3NumParams && !changed; ++i) changed = params[i] != applied[i];
    if (!changed) return;

    const int gainIndex[3] = { kEQ3LowGain, kEQ3MidGain, kEQ3HighGain };
    const int killIndex[3] = { kEQ3KillLow, kEQ3KillMid, kEQ3KillHigh };
    for (int b = 0; b < 3; ++b) {
        const float db = params[gainIndex[b]];
        const bool killed = params[killIndex[b]] > 0.5f || db <= kEQ3KillDb;
        targets[b] = killed ? 0.0f : (db == 0.0f ? 1.0f : loudio::dbToGain(db));
        if (immediate) gains[b].setCurrentAndTargetValue(targets[b]);
        else gains[b].setTargetValue(targets[b]);
    }
    if (immediate || params[kEQ3XoverLowMid] != applied[kEQ3XoverLowMid] || params[kEQ3XoverMidHigh] != applied[kEQ3XoverMidHigh]) {
        splitL.setCrossoverFrequencies(params[kEQ3XoverLowMid], params[kEQ3XoverMidHigh]);
        splitR.setCrossoverFrequencies(params[kEQ3XoverLowMid], params[kEQ3XoverMidHigh]);
        if (immediate) { splitL.snap(); splitR.snap(); splitL.reset(); splitR.reset(); }
    }
    for (int i = 0; i < kEQ3NumParams; ++i) applied[i] = params[i];
}

void EQ3Core::process(float* left, float* right, int numSamples) {
    paramQueue.drainInto(kEQ3ParamTable, params);
    applyParams(false);

    for (int i = 0; i < numSamples; ++i) {
        const float gLow = gains[0].getNextValue(), gMid = gains[1].getNextValue(), gHigh = gains[2].getNextValue();
        double low, mid, high;
        splitL.process(left[i], low, mid, high);
        left[i] = (float)(low * gLow + mid * gMid + high * gHigh);
        splitR.process(right[i], low, mid, high);
        right[i] = (float)(low * gLow + mid * gMid + high * gHigh);
    }
}

bool EQ3Core::getParameterInfo(int index, loudio::ParamMeta* outMeta) const {
    if (!kEQ3ParamTable.valid(index) || !outMeta) return false;
    *outMeta = kEQ3ParamTable[index];
    return true;
}

float EQ3Core::getParameterValue(int index) const {
    return kEQ3ParamTable.valid(index) ? params[index] : 0.0f;
}

void EQ3Core::setParameterValueDirect(int index, float value) {
    if (kEQ3ParamTable.valid(index)) params[index] = value;
}

bool EQ3Core::setParameterValue(int index, float value) {
    return paramQueue.push(kEQ3ParamTable, index, value);
}

bool EQ3Core::setParameterValues(const loudio::ParamUpdate* updates, size_t count) {
    return paramQueue.pushAll(kEQ3ParamTable, updates, count);
}

size_t EQ3Core::getState(void* buffer, size_t bufferSize) const {
    return loudio::StateBlob::write(kEQ3ParamTable, kStateMagic, params, buffer, bufferSize);
}

bool EQ3Core::setState(const void* data, size_t size) {
    return loudio::StateBlob::read(kEQ3ParamTable, kStateMagic, data, size, params);
}
