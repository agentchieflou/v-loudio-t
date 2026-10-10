#include "CompressorCore.h"
#include "loudio/dsp/Decibels.h"
#include "loudio/plugin/StateBlob.h"

#include <algorithm>
#include <cmath>

namespace {
const char kStateMagic[4] = { 'C', 'O', 'M', 'P' };
const float kRmsWindowMs = 20.0f;
} // namespace

CompressorCore::CompressorCore() {
    kCompressorParamTable.fillDefaults(params);
    for (int i = 0; i < kCompressorNumParams; ++i) applied[i] = params[i] - 1.0f; /* force the first applyParams() */
}

int CompressorCore::lookaheadSamplesFor(float ms) const {
    float clamped = std::clamp(ms, 0.0f, kCompressorMaxLookaheadMs);
    return (int)(clamped * 0.001f * (float)sampleRate + 0.5f);
}

void CompressorCore::prepare(double newSampleRate, int /* maxBlockSize */) {
    sampleRate = newSampleRate;
    scHpfL.prepare(sampleRate);
    scHpfR.prepare(sampleRate);
    rmsDetector.setMode(loudio::LevelDetector::Mode::Rms);
    rmsDetector.setAttackRelease(kRmsWindowMs, kRmsWindowMs);
    rmsDetector.prepare(sampleRate);
    const int maxLookahead = lookaheadSamplesFor(kCompressorMaxLookaheadMs);
    lookaheadL.prepare(maxLookahead);
    lookaheadR.prepare(maxLookahead);
    smoothedMakeup.reset(sampleRate, 0.02);
    smoothedMix.reset(sampleRate, 0.02);
    grFast = grSmoothed = 0.0f;
    inputDb = outputDb = -100.0f;
    gainReductionDb = 0.0f;
    for (int i = 0; i < kCompressorNumParams; ++i) applied[i] = params[i] - 1.0f;
    applyParams();
    smoothedMakeup.setCurrentAndTargetValue(loudio::dbToGain(effectiveMakeupDb()));
    smoothedMix.setCurrentAndTargetValue(params[kCompressorMix] * 0.01f);
}

float CompressorCore::effectiveMakeupDb() const {
    if (params[kCompressorAutoMakeup] > 0.5f) return computer.gainReductionDb(0.0f);
    return params[kCompressorMakeup];
}

/* Re-derives only what changed; a block with no edits costs 11 compares. */
void CompressorCore::applyParams() {
    bool changed = false;
    for (int i = 0; i < kCompressorNumParams; ++i) changed = changed || (params[i] != applied[i]);
    if (!changed) return;

    if (params[kCompressorThreshold] != applied[kCompressorThreshold]) computer.setThreshold(params[kCompressorThreshold]);
    if (params[kCompressorRatio] != applied[kCompressorRatio]) computer.setRatio(params[kCompressorRatio]);
    if (params[kCompressorKnee] != applied[kCompressorKnee]) computer.setKneeWidth(params[kCompressorKnee]);
    if (params[kCompressorAttack] != applied[kCompressorAttack]) {
        float ms = std::max(0.01f, params[kCompressorAttack]);
        aAttack = std::exp(-1000.0f / ((float)sampleRate * ms));
    }
    if (params[kCompressorRelease] != applied[kCompressorRelease]) {
        float ms = std::max(0.01f, params[kCompressorRelease]);
        aRelease = std::exp(-1000.0f / ((float)sampleRate * ms));
    }
    if (params[kCompressorLookahead] != applied[kCompressorLookahead]) {
        int n = lookaheadSamplesFor(params[kCompressorLookahead]);
        lookaheadL.setDelaySamples(n);
        lookaheadR.setDelaySamples(n);
    }
    if (params[kCompressorScHpf] != applied[kCompressorScHpf]) {
        scHpfL.setCutoff(params[kCompressorScHpf]);
        scHpfR.setCutoff(params[kCompressorScHpf]);
    }
    /* Makeup and mix always re-target: cheap, and auto makeup depends on
     * threshold/ratio/knee too. */
    smoothedMakeup.setTargetValue(loudio::dbToGain(effectiveMakeupDb()));
    smoothedMix.setTargetValue(std::clamp(params[kCompressorMix], 0.0f, 100.0f) * 0.01f);

    for (int i = 0; i < kCompressorNumParams; ++i) applied[i] = params[i];
}

void CompressorCore::process(float* left, float* right, int numSamples) {
    paramQueue.drainInto(kCompressorParamTable, params);
    applyParams();

    const bool rms = params[kCompressorDetector] > 0.5f;
    const float scHpf = params[kCompressorScHpf];
    float inPeak = 0.0f, outPeak = 0.0f, grMax = 0.0f;

    for (int i = 0; i < numSamples; ++i) {
        const float l = left[i], r = right[i];
        inPeak = std::max(inPeak, std::max(std::abs(l), std::abs(r)));

        /* Sidechain: undelayed, optionally high-passed, stereo-linked. */
        float sl = l, sr = r;
        if (scHpf > 0.0f) { sl = scHpfL.process(l); sr = scHpfR.process(r); }
        float level;
        if (rms) {
            level = rmsDetector.process(std::sqrt(0.5f * (sl * sl + sr * sr)));
        } else {
            level = std::max(std::abs(sl), std::abs(sr));
        }

        /* Gain computer in dB, then the smooth decoupled follower. */
        const float target = computer.gainReductionDb(loudio::gainToDb(level));
        grFast = std::max(target, aRelease * grFast + (1.0f - aRelease) * target);
        grSmoothed = aAttack * grSmoothed + (1.0f - aAttack) * grFast;
        grMax = std::max(grMax, grSmoothed);

        const float makeup = smoothedMakeup.getNextValue();
        const float mix = smoothedMix.getNextValue();
        const float gain = loudio::dbToGain(-grSmoothed) * makeup;

        /* Main path: delayed by the lookahead so the gain arrives in time. */
        const float dl = lookaheadL.process(l), dr = lookaheadR.process(r);
        const float wl = dl * gain, wr = dr * gain;
        left[i] = dl + (wl - dl) * mix;
        right[i] = dr + (wr - dr) * mix;
        outPeak = std::max(outPeak, std::max(std::abs(left[i]), std::abs(right[i])));
    }

    inputDb = loudio::gainToDb(inPeak);
    outputDb = loudio::gainToDb(outPeak);
    gainReductionDb = grMax;
}

bool CompressorCore::getParameterInfo(int index, loudio::ParamMeta* outMeta) const {
    if (!kCompressorParamTable.valid(index) || !outMeta) return false;
    *outMeta = kCompressorParamTable[index];
    return true;
}

float CompressorCore::getParameterValue(int index) const {
    return kCompressorParamTable.valid(index) ? params[index] : 0.0f;
}

void CompressorCore::setParameterValueDirect(int index, float value) {
    if (kCompressorParamTable.valid(index)) params[index] = value;
}

bool CompressorCore::setParameterValue(int index, float value) {
    return paramQueue.push(kCompressorParamTable, index, value);
}

bool CompressorCore::setParameterValues(const loudio::ParamUpdate* updates, size_t count) {
    return paramQueue.pushAll(kCompressorParamTable, updates, count);
}

size_t CompressorCore::getState(void* buffer, size_t bufferSize) const {
    return loudio::StateBlob::write(kCompressorParamTable, kStateMagic, params, buffer, bufferSize);
}

bool CompressorCore::setState(const void* data, size_t size) {
    return loudio::StateBlob::read(kCompressorParamTable, kStateMagic, data, size, params);
}
