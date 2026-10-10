#include "GlueCore.h"
#include "loudio/dsp/Decibels.h"
#include "loudio/plugin/StateBlob.h"

#include <algorithm>
#include <cmath>

namespace {
const char kStateMagic[4] = { 'G', 'L', 'U', 'E' };
const float kAutoFastReleaseMs = 100.0f;
const float kAutoSlowAttackMs = 300.0f;
const float kAutoSlowReleaseMs = 1200.0f;

float onePole(double rate, float ms) { return std::exp(-1000.0f / ((float)rate * std::max(0.01f, ms))); }
} // namespace

GlueCore::GlueCore() {
    kGlueParamTable.fillDefaults(params);
    for (int i = 0; i < kGlueNumParams; ++i) applied[i] = params[i] - 1.0f;
}

void GlueCore::prepare(double newSampleRate, int maxBlockSize) {
    sampleRate = newSampleRate;
    loopRate = sampleRate * kOversampling;
    maxBlock = maxBlockSize < 1 ? 1 : maxBlockSize;
    osL.prepare(maxBlock);
    osR.prepare(maxBlock);
    scHpfL.prepare(loopRate);
    scHpfR.prepare(loopRate);
    dryL.prepare(latencySamples());
    dryR.prepare(latencySamples());
    dryL.setDelaySamples(latencySamples());
    dryR.setDelaySamples(latencySamples());
    dryBufL.assign((size_t)maxBlock, 0.0f);
    dryBufR.assign((size_t)maxBlock, 0.0f);
    smoothedMakeup.reset(sampleRate, 0.02);
    smoothedMix.reset(sampleRate, 0.02);
    aAutoFastRelease = onePole(loopRate, kAutoFastReleaseMs);
    aAutoSlowAttack = onePole(loopRate, kAutoSlowAttackMs);
    aAutoSlowRelease = onePole(loopRate, kAutoSlowReleaseMs);
    grFast = grSlow = grApplied = 0.0f;
    gain = 1.0f;
    inputDb = outputDb = -100.0f;
    gainReductionDb = 0.0f;
    for (int i = 0; i < kGlueNumParams; ++i) applied[i] = params[i] - 1.0f;
    applyParams();
    smoothedMakeup.setCurrentAndTargetValue(loudio::dbToGain(params[kGlueMakeup]));
    smoothedMix.setCurrentAndTargetValue(params[kGlueMix] * 0.01f);
}

void GlueCore::applyParams() {
    bool changed = false;
    for (int i = 0; i < kGlueNumParams; ++i) changed = changed || (params[i] != applied[i]);
    if (!changed) return;

    threshold = params[kGlueThreshold];
    int ratioIndex = std::clamp((int)params[kGlueRatio], 0, kGlueNumRatios - 1);
    slope = kGlueRatios[ratioIndex] - 1.0f;
    int attackIndex = std::clamp((int)params[kGlueAttack], 0, kGlueNumAttacks - 1);
    aAttack = onePole(loopRate, kGlueAttacksMs[attackIndex]);
    int releaseIndex = std::clamp((int)params[kGlueRelease], 0, kGlueNumReleases - 1);
    autoRelease = releaseIndex == kGlueReleaseAuto;
    aRelease = autoRelease ? aAutoFastRelease : onePole(loopRate, kGlueReleasesMs[releaseIndex]);
    if (params[kGlueScHpf] != applied[kGlueScHpf]) {
        scHpfL.setCutoff(params[kGlueScHpf]);
        scHpfR.setCutoff(params[kGlueScHpf]);
    }
    smoothedMakeup.setTargetValue(loudio::dbToGain(params[kGlueMakeup]));
    smoothedMix.setTargetValue(std::clamp(params[kGlueMix], 0.0f, 100.0f) * 0.01f);
    for (int i = 0; i < kGlueNumParams; ++i) applied[i] = params[i];
}

void GlueCore::process(float* left, float* right, int numSamples) {
    paramQueue.drainInto(kGlueParamTable, params);
    applyParams();
    float inPeak = -100.0f, outPeak = -100.0f, gr = 0.0f;
    while (numSamples > 0) {
        int chunk = std::min(numSamples, maxBlock);
        processChunk(left, right, chunk);
        inPeak = std::max(inPeak, inputDb);
        outPeak = std::max(outPeak, outputDb);
        gr = std::max(gr, gainReductionDb);
        left += chunk; right += chunk; numSamples -= chunk;
    }
    inputDb = inPeak; outputDb = outPeak; gainReductionDb = gr;
}

void GlueCore::processChunk(float* left, float* right, int numSamples) {
    const bool hpf = params[kGlueScHpf] > 0.0f;
    float inPeak = 0.0f, outPeak = 0.0f, grMax = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        inPeak = std::max(inPeak, std::max(std::abs(left[i]), std::abs(right[i])));
        dryBufL[(size_t)i] = dryL.process(left[i]);
        dryBufR[(size_t)i] = dryR.process(right[i]);
    }

    float* upL = osL.upsample(left, numSamples);
    float* upR = osR.upsample(right, numSamples);
    const int n = numSamples * kOversampling;
    for (int k = 0; k < n; ++k) {
        /* VCA with the gain decided by the previous loop sample, then the
         * rail's tanh. */
        float yl = kVcaVmax * std::tanh(upL[k] * gain / kVcaVmax);
        float yr = kVcaVmax * std::tanh(upR[k] * gain / kVcaVmax);
        upL[k] = yl;
        upR[k] = yr;

        /* Sidechain taps the output. */
        float sl = yl, sr = yr;
        if (hpf) { sl = scHpfL.process(yl); sr = scHpfR.process(yr); }
        const float level = loudio::gainToDb(std::max(std::abs(sl), std::abs(sr)));
        const float target = slope * std::max(0.0f, level - threshold);

        /* Timing network. */
        if (target > grFast) grFast = aAttack * grFast + (1.0f - aAttack) * target;
        else grFast = aRelease * grFast + (1.0f - aRelease) * target;
        if (autoRelease) {
            if (target > grSlow) grSlow = aAutoSlowAttack * grSlow + (1.0f - aAutoSlowAttack) * target;
            else grSlow = aAutoSlowRelease * grSlow + (1.0f - aAutoSlowRelease) * target;
            grApplied = std::max(grFast, grSlow);
        } else {
            grApplied = grFast;
        }
        grMax = std::max(grMax, grApplied);
        gain = loudio::dbToGain(-grApplied);
    }

    /* Back to the base rate; dry path delayed to match; makeup and mix. */
    osL.downsample(upL, left, numSamples);
    osR.downsample(upR, right, numSamples);
    for (int i = 0; i < numSamples; ++i) {
        const float makeup = smoothedMakeup.getNextValue();
        const float mix = smoothedMix.getNextValue();
        const float wl = left[i] * makeup, wr = right[i] * makeup;
        const float dl = dryBufL[(size_t)i], dr = dryBufR[(size_t)i];
        left[i] = dl + (wl - dl) * mix;
        right[i] = dr + (wr - dr) * mix;
        outPeak = std::max(outPeak, std::max(std::abs(left[i]), std::abs(right[i])));
    }
    inputDb = loudio::gainToDb(inPeak);
    outputDb = loudio::gainToDb(outPeak);
    gainReductionDb = grMax;
}

bool GlueCore::getParameterInfo(int index, loudio::ParamMeta* outMeta) const {
    if (!kGlueParamTable.valid(index) || !outMeta) return false;
    *outMeta = kGlueParamTable[index];
    return true;
}

float GlueCore::getParameterValue(int index) const {
    return kGlueParamTable.valid(index) ? params[index] : 0.0f;
}

void GlueCore::setParameterValueDirect(int index, float value) {
    if (kGlueParamTable.valid(index)) params[index] = value;
}

bool GlueCore::setParameterValue(int index, float value) {
    return paramQueue.push(kGlueParamTable, index, value);
}

bool GlueCore::setParameterValues(const loudio::ParamUpdate* updates, size_t count) {
    return paramQueue.pushAll(kGlueParamTable, updates, count);
}

size_t GlueCore::getState(void* buffer, size_t bufferSize) const {
    return loudio::StateBlob::write(kGlueParamTable, kStateMagic, params, buffer, bufferSize);
}

bool GlueCore::setState(const void* data, size_t size) {
    return loudio::StateBlob::read(kGlueParamTable, kStateMagic, data, size, params);
}
