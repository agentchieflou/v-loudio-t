#include "BusCore.h"
#include "loudio/dsp/Decibels.h"
#include "loudio/plugin/StateBlob.h"

#include <algorithm>
#include <cmath>

namespace {
const char kStateMagic[4] = { 'B', 'U', 'S', '_' };
const float kDcBlockHz = 5.0f;
const float kPi = 3.14159265358979f;
} // namespace

BusCore::BusCore() {
    kBusParamTable.fillDefaults(params);
    for (int i = 0; i < kBusNumParams; ++i) applied[i] = params[i] - 1.0f;
}

void BusCore::panGains(float pan, float& gainL, float& gainR) {
    pan = std::clamp(pan, -1.0f, 1.0f);
    if (pan == 0.0f) { gainL = gainR = 1.0f; return; } /* exactly unity at centre */
    const float t = (pan + 1.0f) * kPi * 0.25f;
    gainL = 1.41421356f * std::cos(t);
    gainR = 1.41421356f * std::sin(t);
    if (gainL < 0.0f) gainL = 0.0f;
}

void BusCore::prepare(double newSampleRate, int /* maxBlockSize */) {
    sampleRate = newSampleRate;
    smoothedGain.reset(sampleRate, 0.02);
    smoothedPanL.reset(sampleRate, 0.02);
    smoothedPanR.reset(sampleRate, 0.02);
    smoothedWidth.reset(sampleRate, 0.02);
    dcL.prepare();
    dcR.prepare();
    dcCoefficient = std::exp(-2.0f * kPi * kDcBlockHz / (float)sampleRate);
    inputDb = outputDb = -100.0f;
    for (int i = 0; i < kBusNumParams; ++i) applied[i] = params[i] - 1.0f;
    applyParams();
    smoothedGain.setCurrentAndTargetValue(loudio::dbToGain(params[kBusGain]));
    float gl, gr;
    panGains(params[kBusPan] * 0.01f, gl, gr);
    smoothedPanL.setCurrentAndTargetValue(gl);
    smoothedPanR.setCurrentAndTargetValue(gr);
    smoothedWidth.setCurrentAndTargetValue(std::clamp(params[kBusWidth], 0.0f, 200.0f) * 0.01f);
}

void BusCore::applyParams() {
    bool changed = false;
    for (int i = 0; i < kBusNumParams; ++i) changed = changed || (params[i] != applied[i]);
    if (!changed) return;
    smoothedGain.setTargetValue(loudio::dbToGain(params[kBusGain]));
    float gl, gr;
    panGains(params[kBusPan] * 0.01f, gl, gr);
    smoothedPanL.setTargetValue(gl);
    smoothedPanR.setTargetValue(gr);
    smoothedWidth.setTargetValue(std::clamp(params[kBusWidth], 0.0f, 200.0f) * 0.01f);
    for (int i = 0; i < kBusNumParams; ++i) applied[i] = params[i];
}

void BusCore::process(float* left, float* right, int numSamples) {
    paramQueue.drainInto(kBusParamTable, params);
    applyParams();

    const bool swap = params[kBusSwap] > 0.5f;
    const float signL = params[kBusPhaseL] > 0.5f ? -1.0f : 1.0f;
    const float signR = params[kBusPhaseR] > 0.5f ? -1.0f : 1.0f;
    const bool mono = params[kBusMono] > 0.5f;
    const bool dc = params[kBusDcBlock] > 0.5f;
    float inPeak = 0.0f, outPeak = 0.0f;

    for (int i = 0; i < numSamples; ++i) {
        float l = left[i], r = right[i];
        inPeak = std::max(inPeak, std::max(std::abs(l), std::abs(r)));
        if (swap) std::swap(l, r);
        l *= signL;
        r *= signR;
        if (mono) l = r = 0.5f * (l + r);

        const float width = smoothedWidth.getNextValue();
        if (width != 1.0f) {
            const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * width;
            l = mid + side;
            r = mid - side;
        }

        const float gl = smoothedPanL.getNextValue(), gr = smoothedPanR.getNextValue();
        const float g = smoothedGain.getNextValue();
        l *= gl * g;
        r *= gr * g;

        if (dc) {
            l -= dcL.process(l, dcCoefficient);
            r -= dcR.process(r, dcCoefficient);
        }
        left[i] = l;
        right[i] = r;
        outPeak = std::max(outPeak, std::max(std::abs(l), std::abs(r)));
    }
    inputDb = loudio::gainToDb(inPeak);
    outputDb = loudio::gainToDb(outPeak);
}

bool BusCore::getParameterInfo(int index, loudio::ParamMeta* outMeta) const {
    if (!kBusParamTable.valid(index) || !outMeta) return false;
    *outMeta = kBusParamTable[index];
    return true;
}

float BusCore::getParameterValue(int index) const {
    return kBusParamTable.valid(index) ? params[index] : 0.0f;
}

void BusCore::setParameterValueDirect(int index, float value) {
    if (kBusParamTable.valid(index)) params[index] = value;
}

bool BusCore::setParameterValue(int index, float value) {
    return paramQueue.push(kBusParamTable, index, value);
}

bool BusCore::setParameterValues(const loudio::ParamUpdate* updates, size_t count) {
    return paramQueue.pushAll(kBusParamTable, updates, count);
}

size_t BusCore::getState(void* buffer, size_t bufferSize) const {
    return loudio::StateBlob::write(kBusParamTable, kStateMagic, params, buffer, bufferSize);
}

bool BusCore::setState(const void* data, size_t size) {
    return loudio::StateBlob::read(kBusParamTable, kStateMagic, data, size, params);
}
