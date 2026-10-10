#include "LimiterCore.h"
#include "loudio/dsp/Decibels.h"
#include "loudio/plugin/StateBlob.h"

#include <algorithm>
#include <cmath>

namespace {
const char kStateMagic[4] = { 'L', 'I', 'M', 'I' };
/* The required gain is computed against the ceiling less this much, so
 * float rounding in the box average and the final multiply can never
 * land a sample above the ceiling itself. -0.0001 dB. */
const float kCeilingMargin = 0.99999f;
} // namespace

LimiterCore::LimiterCore() {
    kLimiterParamTable.fillDefaults(params);
    for (int i = 0; i < kLimiterNumParams; ++i) applied[i] = params[i] - 1.0f;
}

int LimiterCore::samplesFor(float ms) const {
    float clamped = std::clamp(ms, 0.0f, kLimiterMaxLookaheadMs);
    return (int)(clamped * 0.001f * (float)sampleRate + 0.5f);
}

void LimiterCore::Chain::allocate(int capacity) {
    minValues.assign((size_t)capacity, 1.0f);
    minDeque.assign((size_t)capacity, 0);
    boxValues.assign((size_t)capacity, 1.0f);
    reset(1);
}

void LimiterCore::Chain::reset(int boxLength) {
    std::fill(minValues.begin(), minValues.end(), 1.0f);
    dequeHead = dequeCount = 0;
    written = 0;
    std::fill(boxValues.begin(), boxValues.end(), 1.0f);
    boxIndex = 0;
    boxSum = (double)boxLength;
    release = 1.0f;
}

/* Sliding-window minimum over the last `window` values, O(1) amortised:
 * the deque holds absolute indices of candidates whose values are
 * non-decreasing from front to back; the front is the minimum. */
float LimiterCore::Chain::windowMin(float g, int window) {
    const int cap = (int)minValues.size();
    const long long now = written++;
    minValues[(size_t)(now % cap)] = g;
    /* Candidates >= g can never be the minimum again. */
    while (dequeCount > 0) {
        long long back = minDeque[(size_t)((dequeHead + dequeCount - 1) % cap)];
        if (minValues[(size_t)(back % cap)] >= g) --dequeCount;
        else break;
    }
    minDeque[(size_t)((dequeHead + dequeCount) % cap)] = now;
    ++dequeCount;
    /* The front falls out once it is `window` samples old. */
    if (minDeque[(size_t)dequeHead] <= now - window) {
        dequeHead = (dequeHead + 1) % cap;
        --dequeCount;
    }
    return minValues[(size_t)(minDeque[(size_t)dequeHead] % cap)];
}

float LimiterCore::Chain::box(float m, int length) {
    boxSum += (double)m - (double)boxValues[(size_t)boxIndex];
    boxValues[(size_t)boxIndex] = m;
    boxIndex = (boxIndex + 1) % length;
    return (float)(boxSum / (double)length);
}

void LimiterCore::prepare(double newSampleRate, int maxBlockSize) {
    sampleRate = newSampleRate;
    maxBlock = maxBlockSize < 1 ? 1 : maxBlockSize;
    maxWindow = samplesFor(kLimiterMaxLookaheadMs) + 1;
    const int maxDelay = maxWindow - 1 + detectorDelaySamples(kLimiterOs4x);
    delayL.prepare(maxDelay);
    delayR.prepare(maxDelay);
    delayL.setDelaySamples(maxDelay);
    delayR.setDelaySamples(maxDelay);
    peakDelayL.prepare(maxDelay);
    peakDelayR.prepare(maxDelay);
    os2L.prepare(maxBlock); os2R.prepare(maxBlock);
    os4L.prepare(maxBlock); os4R.prepare(maxBlock);
    chains[0].allocate(maxWindow);
    chains[1].allocate(maxWindow);
    inputDb = outputDb = -100.0f;
    gainReductionDb = 0.0f;
    for (int i = 0; i < kLimiterNumParams; ++i) applied[i] = params[i] - 1.0f;
    applyParams();
}

void LimiterCore::applyParams() {
    bool changed = false;
    for (int i = 0; i < kLimiterNumParams; ++i) changed = changed || (params[i] != applied[i]);
    if (!changed) return;

    ceilingGain = loudio::dbToGain(std::clamp(params[kLimiterCeiling], -30.0f, 0.0f)) * kCeilingMargin;
    inputGain = loudio::dbToGain(params[kLimiterInputGain]);
    link = std::clamp(params[kLimiterLink], 0.0f, 100.0f) * 0.01f;
    aRelease = std::exp(-1000.0f / ((float)sampleRate * std::max(0.01f, params[kLimiterRelease])));
    int newOs = std::clamp((int)params[kLimiterOversampling], 0, kLimiterNumOs - 1);

    if (params[kLimiterLookahead] != applied[kLimiterLookahead] || newOs != oversampling) {
        /* Resizing the window: restart the gain state (a one-off step at
         * the moment the user changes lookahead or oversampling). */
        const int L = samplesFor(params[kLimiterLookahead]);
        window = L + 1;
        boxLength = std::max(1, L);
        oversampling = newOs;
        chains[0].reset(boxLength);
        chains[1].reset(boxLength);
        os2L.clear(); os2R.clear(); os4L.clear(); os4R.clear();
        const int remainder = delayL.latencySamples() - L - detectorDelaySamples(oversampling);
        peakDelayL.setDelaySamples(remainder);
        peakDelayR.setDelaySamples(remainder);
        peakDelayL.clear();
        peakDelayR.clear();
    }
    for (int i = 0; i < kLimiterNumParams; ++i) applied[i] = params[i];
}

void LimiterCore::process(float* left, float* right, int numSamples) {
    paramQueue.drainInto(kLimiterParamTable, params);
    applyParams();
    /* The oversamplers are sized for maxBlockSize; a host that sends more
     * (it happens) is served in chunks. */
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

void LimiterCore::processChunk(float* left, float* right, int numSamples) {
    /* Input gain in place, then the (optional) oversampled peak source. */
    for (int i = 0; i < numSamples; ++i) { left[i] *= inputGain; right[i] *= inputGain; }
    const float* upL = nullptr; const float* upR = nullptr;
    int factor = 1;
    if (oversampling == kLimiterOs2x) { upL = os2L.upsample(left, numSamples); upR = os2R.upsample(right, numSamples); factor = 2; }
    else if (oversampling == kLimiterOs4x) { upL = os4L.upsample(left, numSamples); upR = os4R.upsample(right, numSamples); factor = 4; }

    float inPeak = 0.0f, outPeak = 0.0f, minGain = 1.0f;
    for (int i = 0; i < numSamples; ++i) {
        const float l = left[i], r = right[i];
        inPeak = std::max(inPeak, std::max(std::abs(l), std::abs(r)));

        float peakL, peakR;
        if (factor == 1) { peakL = std::abs(l); peakR = std::abs(r); }
        else {
            peakL = peakR = 0.0f;
            for (int k = 0; k < factor; ++k) {
                peakL = std::max(peakL, std::abs(upL[i * factor + k]));
                peakR = std::max(peakR, std::abs(upR[i * factor + k]));
            }
        }
        /* Through the remainder delay (its zeros are silence = unity gain). */
        peakL = peakDelayL.process(peakL);
        peakR = peakDelayR.process(peakR);
        float gL = peakL > ceilingGain ? ceilingGain / peakL : 1.0f;
        float gR = peakR > ceilingGain ? ceilingGain / peakR : 1.0f;
        const float gLinked = std::min(gL, gR);
        gL += (gLinked - gL) * link;
        gR += (gLinked - gR) * link;

        float g[2] = { gL, gR };
        for (int c = 0; c < 2; ++c) {
            Chain& ch = chains[c];
            float m = ch.windowMin(g[c], window);
            float b = ch.box(m, boxLength);
            float rel = aRelease * ch.release + (1.0f - aRelease);
            ch.release = std::min(b, rel);
            g[c] = ch.release;
        }
        minGain = std::min(minGain, std::min(g[0], g[1]));

        left[i] = delayL.process(l) * g[0];
        right[i] = delayR.process(r) * g[1];
        outPeak = std::max(outPeak, std::max(std::abs(left[i]), std::abs(right[i])));
    }

    inputDb = loudio::gainToDb(inPeak);
    outputDb = loudio::gainToDb(outPeak);
    gainReductionDb = -loudio::gainToDb(minGain);
}

bool LimiterCore::getParameterInfo(int index, loudio::ParamMeta* outMeta) const {
    if (!kLimiterParamTable.valid(index) || !outMeta) return false;
    *outMeta = kLimiterParamTable[index];
    return true;
}

float LimiterCore::getParameterValue(int index) const {
    return kLimiterParamTable.valid(index) ? params[index] : 0.0f;
}

void LimiterCore::setParameterValueDirect(int index, float value) {
    if (kLimiterParamTable.valid(index)) params[index] = value;
}

bool LimiterCore::setParameterValue(int index, float value) {
    return paramQueue.push(kLimiterParamTable, index, value);
}

bool LimiterCore::setParameterValues(const loudio::ParamUpdate* updates, size_t count) {
    return paramQueue.pushAll(kLimiterParamTable, updates, count);
}

size_t LimiterCore::getState(void* buffer, size_t bufferSize) const {
    return loudio::StateBlob::write(kLimiterParamTable, kStateMagic, params, buffer, bufferSize);
}

bool LimiterCore::setState(const void* data, size_t size) {
    return loudio::StateBlob::read(kLimiterParamTable, kStateMagic, data, size, params);
}
