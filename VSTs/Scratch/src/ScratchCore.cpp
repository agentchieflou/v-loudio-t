#include "ScratchCore.h"
#include "loudio/plugin/StateBlob.h"

namespace {
const char kStateMagic[4] = { 'S', 'C', 'R', 'A' };
} // namespace

ScratchCore::ScratchCore() {
    kScratchParamTable.fillDefaults(params);
}

void ScratchCore::prepare(double newSampleRate, int /* maxBlockSize */) {
    sampleRate = newSampleRate;
}

void ScratchCore::process(float* left, float* right, int numSamples) {
    paramQueue.drainInto(kScratchParamTable, params);

    const float gain = params[kScratchGain];
    for (int i = 0; i < numSamples; ++i) {
        left[i] *= gain;
        right[i] *= gain;
    }
}

bool ScratchCore::getParameterInfo(int index, loudio::ParamMeta* outMeta) const {
    if (!kScratchParamTable.valid(index) || !outMeta) return false;
    *outMeta = kScratchParamTable[index];
    return true;
}

float ScratchCore::getParameterValue(int index) const {
    return kScratchParamTable.valid(index) ? params[index] : 0.0f;
}

void ScratchCore::setParameterValueDirect(int index, float value) {
    if (kScratchParamTable.valid(index)) params[index] = value;
}

bool ScratchCore::setParameterValue(int index, float value) {
    return paramQueue.push(kScratchParamTable, index, value);
}

bool ScratchCore::setParameterValues(const loudio::ParamUpdate* updates, size_t count) {
    return paramQueue.pushAll(kScratchParamTable, updates, count);
}

size_t ScratchCore::getState(void* buffer, size_t bufferSize) const {
    return loudio::StateBlob::write(kScratchParamTable, kStateMagic, params, buffer, bufferSize);
}

bool ScratchCore::setState(const void* data, size_t size) {
    return loudio::StateBlob::read(kScratchParamTable, kStateMagic, data, size, params);
}
