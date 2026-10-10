#include "GateCore.h"
#include "loudio/dsp/Decibels.h"
#include "loudio/plugin/StateBlob.h"

#include <algorithm>
#include <cmath>

namespace {
const char kStateMagic[4] = { 'G', 'A', 'T', 'E' };
const float kDetectorAttackMs = 0.01f;
const float kDetectorReleaseMs = 5.0f;
} // namespace

GateCore::GateCore() {
    kGateParamTable.fillDefaults(params);
    for (int i = 0; i < kGateNumParams; ++i) applied[i] = params[i] - 1.0f; /* force the first applyParams() */
}

int GateCore::lookaheadSamplesFor(float ms) const {
    float clamped = std::clamp(ms, 0.0f, kGateMaxLookaheadMs);
    return (int)(clamped * 0.001f * (float)sampleRate + 0.5f);
}

void GateCore::prepare(double newSampleRate, int /* maxBlockSize */) {
    sampleRate = newSampleRate;
    scHpfL.prepare(sampleRate);
    scHpfR.prepare(sampleRate);
    detector.setMode(loudio::LevelDetector::Mode::Peak);
    detector.setAttackRelease(kDetectorAttackMs, kDetectorReleaseMs);
    detector.prepare(sampleRate);
    hysteresis.reset(false);
    const int maxLookahead = lookaheadSamplesFor(kGateMaxLookaheadMs);
    lookaheadL.prepare(maxLookahead);
    lookaheadR.prepare(maxLookahead);
    holdCounter = 0;
    open = false;
    inputDb = -100.0f;
    gainReductionDb = 0.0f;
    for (int i = 0; i < kGateNumParams; ++i) applied[i] = params[i] - 1.0f;
    applyParams();
    gain = floorGain; /* starts closed */
}

void GateCore::applyParams() {
    bool changed = false;
    for (int i = 0; i < kGateNumParams; ++i) changed = changed || (params[i] != applied[i]);
    if (!changed) return;

    hysteresis.setThresholds(params[kGateThreshold], params[kGateThreshold] - std::max(0.0f, params[kGateHysteresis]));
    aAttack = std::exp(-1000.0f / ((float)sampleRate * std::max(0.001f, params[kGateAttack])));
    aRelease = std::exp(-1000.0f / ((float)sampleRate * std::max(0.001f, params[kGateRelease])));
    holdSamples = (int)(std::max(0.0f, params[kGateHold]) * 0.001f * (float)sampleRate + 0.5f);
    float range = std::clamp(params[kGateRange], kGateMinRangeDb, 0.0f);
    floorGain = range <= kGateMinRangeDb ? 0.0f : loudio::dbToGain(range);
    if (params[kGateScHpf] != applied[kGateScHpf]) {
        scHpfL.setCutoff(params[kGateScHpf]);
        scHpfR.setCutoff(params[kGateScHpf]);
    }
    if (params[kGateLookahead] != applied[kGateLookahead]) {
        int n = lookaheadSamplesFor(params[kGateLookahead]);
        lookaheadL.setDelaySamples(n);
        lookaheadR.setDelaySamples(n);
    }
    for (int i = 0; i < kGateNumParams; ++i) applied[i] = params[i];
}

void GateCore::process(float* left, float* right, int numSamples) {
    paramQueue.drainInto(kGateParamTable, params);
    applyParams();

    const float scHpf = params[kGateScHpf];
    float inPeak = 0.0f, minGain = 1.0f;

    for (int i = 0; i < numSamples; ++i) {
        const float l = left[i], r = right[i];
        inPeak = std::max(inPeak, std::max(std::abs(l), std::abs(r)));

        float sl = l, sr = r;
        if (scHpf > 0.0f) { sl = scHpfL.process(l); sr = scHpfR.process(r); }
        const float levelDb = detector.processDb(std::max(std::abs(sl), std::abs(sr)));

        /* Open/close decision with hysteresis; closing is deferred by the hold. */
        if (hysteresis.process(levelDb)) {
            open = true;
            holdCounter = holdSamples;
        } else if (open) {
            if (holdCounter > 0) --holdCounter;
            else open = false;
        }

        /* Linear one-pole toward 1 or the floor. */
        if (open) gain = aAttack * gain + (1.0f - aAttack) * 1.0f;
        else gain = aRelease * gain + (1.0f - aRelease) * floorGain;
        minGain = std::min(minGain, gain);

        left[i] = lookaheadL.process(l) * gain;
        right[i] = lookaheadR.process(r) * gain;
    }

    inputDb = loudio::gainToDb(inPeak);
    gainReductionDb = -loudio::gainToDb(minGain);
}

bool GateCore::getParameterInfo(int index, loudio::ParamMeta* outMeta) const {
    if (!kGateParamTable.valid(index) || !outMeta) return false;
    *outMeta = kGateParamTable[index];
    return true;
}

float GateCore::getParameterValue(int index) const {
    return kGateParamTable.valid(index) ? params[index] : 0.0f;
}

void GateCore::setParameterValueDirect(int index, float value) {
    if (kGateParamTable.valid(index)) params[index] = value;
}

bool GateCore::setParameterValue(int index, float value) {
    return paramQueue.push(kGateParamTable, index, value);
}

bool GateCore::setParameterValues(const loudio::ParamUpdate* updates, size_t count) {
    return paramQueue.pushAll(kGateParamTable, updates, count);
}

size_t GateCore::getState(void* buffer, size_t bufferSize) const {
    return loudio::StateBlob::write(kGateParamTable, kStateMagic, params, buffer, bufferSize);
}

bool GateCore::setState(const void* data, size_t size) {
    return loudio::StateBlob::read(kGateParamTable, kStateMagic, data, size, params);
}
