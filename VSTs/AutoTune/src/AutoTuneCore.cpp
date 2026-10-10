#include "AutoTuneCore.h"
#include "loudio/dsp/Decibels.h"
#include "loudio/plugin/StateBlob.h"

#include <algorithm>
#include <cmath>

namespace {
const char kStateMagic[4] = { 'A', 'T', 'U', 'N' };
} // namespace

AutoTuneCore::AutoTuneCore() {
    kAutoTuneParamTable.fillDefaults(params);
    for (int i = 0; i < kAutoTuneNumParams; ++i) applied[i] = params[i] - 1.0f;
}

void AutoTuneCore::prepare(double newSampleRate, int /* maxBlockSize */) {
    sampleRate = newSampleRate;
    const int maxPeriod = (int)std::ceil(sampleRate / (double)kAutoTuneAbsoluteMinHz);
    yin.prepare(sampleRate, kYinWindow, kAutoTuneAbsoluteMinHz, 1500.0f);
    shifterL.prepare(sampleRate, maxPeriod);
    shifterR.prepare(sampleRate, maxPeriod);
    dryL.prepare(shifterL.latencySamples());
    dryR.prepare(shifterR.latencySamples());
    dryL.setDelaySamples(shifterL.latencySamples());
    dryR.setDelaySamples(shifterR.latencySamples());
    int ringSize = 1;
    while (ringSize < yin.requiredSamples() + kHop) ringSize <<= 1;
    ringMask = ringSize - 1;
    analysisRing.assign((size_t)ringSize, 0.0f);
    analysisFrame.assign((size_t)yin.requiredSamples(), 0.0f);
    written = 0;
    sinceHop = 0;
    smoothedMix.reset(sampleRate, 0.02);
    logRatio = 0.0f;
    voiced = wasVoiced = false;
    detectedF0 = 0.0f;
    appliedCents = 0.0f;
    for (int i = 0; i < kAutoTuneNumParams; ++i) applied[i] = params[i] - 1.0f;
    applyParams();
    smoothedMix.setCurrentAndTargetValue(params[kAutoTuneMix] * 0.01f);
}

void AutoTuneCore::applyParams() {
    bool changed = false;
    for (int i = 0; i < kAutoTuneNumParams && !changed; ++i) changed = params[i] != applied[i];
    if (!changed) return;
    key = std::clamp((int)params[kAutoTuneKey], 0, 11);
    scaleMask = kAutoTuneScaleMasks[std::clamp((int)params[kAutoTuneScale], 0, kAutoTuneNumScales - 1)];
    retuneMs = std::max(0.0f, params[kAutoTuneRetuneSpeed]);
    humanise = std::clamp(params[kAutoTuneHumanise], 0.0f, 100.0f) * 0.01f;
    reference = std::clamp(params[kAutoTuneReference], 400.0f, 480.0f);
    const AutoTuneRange& range = kAutoTuneRanges[std::clamp((int)params[kAutoTuneInputType], 0, kAutoTuneNumInputTypes - 1)];
    yin.setRange(range.minHz, range.maxHz);
    const float formantRatio = std::exp2(std::clamp(params[kAutoTuneFormant], -12.0f, 12.0f) / 12.0f);
    shifterL.setFormantRatio(formantRatio);
    shifterR.setFormantRatio(formantRatio);
    smoothedMix.setTargetValue(std::clamp(params[kAutoTuneMix], 0.0f, 100.0f) * 0.01f);
    for (int i = 0; i < kAutoTuneNumParams; ++i) applied[i] = params[i];
}

int AutoTuneCore::quantise(float midiReal, int key, int scaleMask) {
    const int centre = (int)std::lround(midiReal);
    int best = centre;
    float bestDist = 1e9f;
    for (int n = centre - 6; n <= centre + 6; ++n) {
        int pc = ((n - key) % 12 + 12) % 12;
        if (!(scaleMask & (1 << pc))) continue;
        float d = std::abs((float)n - midiReal);
        if (d < bestDist) { bestDist = d; best = n; }
    }
    return best;
}

void AutoTuneCore::analyse() {
    const int need = yin.requiredSamples();
    const long long start = written - need;
    double energy = 0.0;
    for (int i = 0; i < need; ++i) {
        float v = analysisRing[(size_t)((start + i) & ringMask)];
        analysisFrame[(size_t)i] = v;
        energy += (double)v * v;
    }
    const float rmsDb = loudio::gainToDb((float)std::sqrt(energy / (double)need));
    float hz = 0.0f, aperiodicity = 1.0f;
    bool periodic = rmsDb > kSilenceDb && yin.estimate(analysisFrame.data(), hz, aperiodicity);
    voiced = periodic;
    float targetCents = 0.0f;
    if (voiced) {
        detectedF0 = hz;
        detectedMidiReal = 69.0f + 12.0f * std::log2(hz / reference);
        targetNote = quantise(detectedMidiReal, key, scaleMask);
        targetCents = ((float)targetNote - detectedMidiReal) * 100.0f * (1.0f - humanise);
        /* Retune: instant on a new note (unvoiced -> voiced), otherwise a
         * one-pole toward the target with the retune speed. */
        if (!wasVoiced || retuneMs <= 0.0f) {
            logRatio = targetCents;
        } else {
            const float hopMs = 1000.0f * (float)kHop / (float)sampleRate;
            const float alpha = 1.0f - std::exp(-hopMs / retuneMs);
            logRatio += (targetCents - logRatio) * alpha;
        }
        appliedCents = logRatio;
        const float ratio = std::exp2(appliedCents / 1200.0f);
        const float period = (float)(sampleRate / (double)hz);
        shifterL.setPeriodAndRatio(period, ratio);
        shifterR.setPeriodAndRatio(period, ratio);
    } else {
        appliedCents = 0.0f;
        shifterL.setPeriodAndRatio(0.0f, 1.0f);
        shifterR.setPeriodAndRatio(0.0f, 1.0f);
    }
    wasVoiced = voiced;
}

void AutoTuneCore::process(float* left, float* right, int numSamples) {
    paramQueue.drainInto(kAutoTuneParamTable, params);
    applyParams();

    for (int i = 0; i < numSamples; ++i) {
        const float inL = left[i], inR = right[i];
        analysisRing[(size_t)(written & ringMask)] = 0.5f * (inL + inR);
        ++written;
        if (++sinceHop >= kHop) { sinceHop = 0; analyse(); }

        const float wetL = shifterL.process(inL);
        const float wetR = shifterR.process(inR);
        const float dl = dryL.process(inL), dr = dryR.process(inR);
        const float mix = smoothedMix.getNextValue();
        left[i] = dl + (wetL - dl) * mix;
        right[i] = dr + (wetR - dr) * mix;
    }
}

bool AutoTuneCore::getParameterInfo(int index, loudio::ParamMeta* outMeta) const {
    if (!kAutoTuneParamTable.valid(index) || !outMeta) return false;
    *outMeta = kAutoTuneParamTable[index];
    return true;
}

float AutoTuneCore::getParameterValue(int index) const {
    return kAutoTuneParamTable.valid(index) ? params[index] : 0.0f;
}

void AutoTuneCore::setParameterValueDirect(int index, float value) {
    if (kAutoTuneParamTable.valid(index)) params[index] = value;
}

bool AutoTuneCore::setParameterValue(int index, float value) {
    return paramQueue.push(kAutoTuneParamTable, index, value);
}

bool AutoTuneCore::setParameterValues(const loudio::ParamUpdate* updates, size_t count) {
    return paramQueue.pushAll(kAutoTuneParamTable, updates, count);
}

size_t AutoTuneCore::getState(void* buffer, size_t bufferSize) const {
    return loudio::StateBlob::write(kAutoTuneParamTable, kStateMagic, params, buffer, bufferSize);
}

bool AutoTuneCore::setState(const void* data, size_t size) {
    return loudio::StateBlob::read(kAutoTuneParamTable, kStateMagic, data, size, params);
}
