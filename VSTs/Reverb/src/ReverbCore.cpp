#include "ReverbCore.h"

#include <cmath>
#include <cstring>
#include <cstdint>
#include <vector>
#include <algorithm>

static const float baseDelayLengths[8] = { 1013.0f, 1123.0f, 1277.0f, 1399.0f, 1511.0f, 1693.0f, 1823.0f, 1999.0f };

ReverbCore::ReverbCore() {
    for (int i = 0; i < kNumParams; ++i) {
        params[i] = kReverbParams[i].defaultValue;
    }
    cuif_spsc_init(&paramUpdateRingBuffer, paramUpdateStorage, sizeof(paramUpdateStorage));
}

void ReverbCore::prepare(double newSampleRate, int /* maxBlockSize */) {
    sampleRate = newSampleRate;
    float rateScale = (float)(sampleRate / 44100.0);

    /* 1. Pre-Delay (max 150ms) */
    int maxPreDelaySamples = (int)(0.150f * sampleRate) + 2;
    preDelayLeft.prepare(maxPreDelaySamples);
    preDelayRight.prepare(maxPreDelaySamples);

    /* 2. Schroeder Diffusers */
    std::vector<int> leftDiffLengths = {
        (int)(151 * rateScale),
        (int)(307 * rateScale),
        (int)(439 * rateScale),
        (int)(571 * rateScale)
    };
    diffuserLeft.prepare(leftDiffLengths, 0.6f);

    std::vector<int> rightDiffLengths = {
        (int)(163 * rateScale),
        (int)(313 * rateScale),
        (int)(443 * rateScale),
        (int)(577 * rateScale)
    };
    diffuserRight.prepare(rightDiffLengths, 0.6f);

    /* 3. Early Reflections */
    std::vector<ReverbTap> leftTaps = {
        { (int)(353 * rateScale), 0.7f },
        { (int)(563 * rateScale), -0.5f },
        { (int)(827 * rateScale), 0.4f },
        { (int)(1093 * rateScale), -0.3f }
    };
    earlyReflectionsLeft.prepare((int)(1200 * rateScale), leftTaps);

    std::vector<ReverbTap> rightTaps = {
        { (int)(389 * rateScale), -0.7f },
        { (int)(599 * rateScale), 0.5f },
        { (int)(859 * rateScale), -0.4f },
        { (int)(1117 * rateScale), 0.3f }
    };
    earlyReflectionsRight.prepare((int)(1200 * rateScale), rightTaps);

    /* 4. FDN Late Reverb & Crossovers */
    float fc1 = params[kParamCrossoverLowMid];
    float fc2 = params[kParamCrossoverMidHigh];
    for (int i = 0; i < 8; ++i) {
        int delayLen = (int)(baseDelayLengths[i] * rateScale * 1.5f);
        fdnDelayLines[i].prepare(delayLen + 2);
        fdnDampingFilters[i].prepare();
        crossovers[i].prepare();
        crossovers[i].setCrossoverFrequencies((float)sampleRate, fc1, fc2);
    }

    /* 5. Post EQ biquads */
    updatePostEQ();

    /* 6. Envelope detectors */
    duckDetector.prepare(sampleRate);
    gateDetector.prepare(sampleRate);
    gateFade = 1.0f;
    gateTimerMs = 0.0f;

    /* 7. Real-time FFT analyzers */
    fftAnalyzerLeft.prepare(sampleRate);
    fftAnalyzerRight.prepare(sampleRate);

    /* 8. Parameter smoothing */
    smoothedPreDelayMs.reset(sampleRate, 0.02);
    smoothedDecayTimeSec.reset(sampleRate, 0.02);
    smoothedDamping.reset(sampleRate, 0.02);
    smoothedWidth.reset(sampleRate, 0.02);
    smoothedDryWet.reset(sampleRate, 0.02);
    smoothedDistance.reset(sampleRate, 0.02);
    smoothedThickness.reset(sampleRate, 0.02);

    smoothedPreDelayMs.setCurrentAndTargetValue(params[kParamPreDelay]);
    smoothedDecayTimeSec.setCurrentAndTargetValue(params[kParamDecayTime]);
    smoothedDamping.setCurrentAndTargetValue(params[kParamDamping]);
    smoothedWidth.setCurrentAndTargetValue(params[kParamWidth]);
    smoothedDryWet.setCurrentAndTargetValue(params[kParamDryWet]);
    smoothedDistance.setCurrentAndTargetValue(params[kParamDistance]);
    smoothedThickness.setCurrentAndTargetValue(params[kParamThickness]);
}

void ReverbCore::updatePostEQ() {
    float lowGain = params[kParamPostEqLowGain];
    float midGain = params[kParamPostEqMidGain];
    float highGain = params[kParamPostEqHighGain];

    postEqLeftLow.makeLowShelf(sampleRate, 150.0f, 0.707f, lowGain);
    postEqRightLow.makeLowShelf(sampleRate, 150.0f, 0.707f, lowGain);

    postEqLeftMid.makePeakFilter(sampleRate, 1500.0f, 0.707f, midGain);
    postEqRightMid.makePeakFilter(sampleRate, 1500.0f, 0.707f, midGain);

    postEqLeftHigh.makeHighShelf(sampleRate, 8000.0f, 0.707f, highGain);
    postEqRightHigh.makeHighShelf(sampleRate, 8000.0f, 0.707f, highGain);
}

void ReverbCore::drainPendingParameterUpdates() {
    ParamUpdateMessage msg;
    while (cuif_spsc_readable(&paramUpdateRingBuffer) >= sizeof(msg)) {
        if (cuif_spsc_read(&paramUpdateRingBuffer, (unsigned char*)&msg, sizeof(msg)) != sizeof(msg)) break;
        if (msg.index >= 0 && msg.index < kNumParams) {
            params[msg.index] = msg.value;
        }
    }
}

void ReverbCore::process(float* leftChannel, float* rightChannel, int numSamples) {
    drainPendingParameterUpdates();

    smoothedPreDelayMs.setTargetValue(params[kParamPreDelay]);
    smoothedDecayTimeSec.setTargetValue(params[kParamDecayTime]);
    smoothedDamping.setTargetValue(params[kParamDamping]);
    smoothedWidth.setTargetValue(params[kParamWidth]);
    smoothedDryWet.setTargetValue(params[kParamDryWet]);
    smoothedDistance.setTargetValue(params[kParamDistance]);
    smoothedThickness.setTargetValue(params[kParamThickness]);

    float fc1 = params[kParamCrossoverLowMid];
    float fc2 = params[kParamCrossoverMidHigh];
    for (int i = 0; i < 8; ++i) {
        crossovers[i].setCrossoverFrequencies((float)sampleRate, fc1, fc2);
    }

    updatePostEQ();

    float decayLow = params[kParamDecayLow];
    float decayMid = params[kParamDecayMid];
    float decayHigh = params[kParamDecayHigh];

    float duckThreshold = params[kParamDuckThreshold];
    float duckAmount = params[kParamDuckAmount];
    float duckRelease = params[kParamDuckRelease];
    duckDetector.setAttackRelease(10.0f, duckRelease);

    float gateThreshold = params[kParamGateThreshold];
    float gateTime = params[kParamGateTime];

    bool freeze = params[kParamFreeze] > 0.5f;
    int mode = (int)params[kParamMode];

    float rateScale = (float)(sampleRate / 44100.0f);

    float modeScale = 1.0f;
    if (mode == 0)      modeScale = 0.6f;
    else if (mode == 1) modeScale = 1.0f;
    else if (mode == 2) modeScale = 0.75f;
    else if (mode == 3) modeScale = 1.5f;
    else if (mode == 4) modeScale = 0.85f;

    for (int sample = 0; sample < numSamples; ++sample) {
        float preDelayMs = smoothedPreDelayMs.getNextValue();
        float decayTimeSec = smoothedDecayTimeSec.getNextValue();
        float damping = smoothedDamping.getNextValue();
        float width = smoothedWidth.getNextValue();
        float dryWet = smoothedDryWet.getNextValue();
        float distance = smoothedDistance.getNextValue();
        float thickness = smoothedThickness.getNextValue();

        float inL = leftChannel[sample];
        float inR = rightChannel[sample];

        /* Pre-Delay */
        float preDelaySamples = preDelayMs * 0.001f * (float)sampleRate;
        preDelayLeft.write(inL);
        preDelayRight.write(inR);
        float preL = preDelayLeft.read(preDelaySamples);
        float preR = preDelayRight.read(preDelaySamples);

        /* All-Pass Diffusers */
        float diffL = diffuserLeft.process(preL);
        float diffR = diffuserRight.process(preR);

        /* Early Reflections */
        float earlyL = earlyReflectionsLeft.process(diffL);
        float earlyR = earlyReflectionsRight.process(diffR);

        /* Late FDN Reverb Loop */
        float currentDecay = decayTimeSec;
        float currentDamping = damping;
        if (freeze) {
            currentDecay = 1000.0f;
            currentDamping = 0.0f;
        }

        float loopGains[8];
        for (int i = 0; i < 8; ++i) {
            float delayInSamples = baseDelayLengths[i] * rateScale * modeScale;
            if (freeze) {
                loopGains[i] = 1.0f;
            } else {
                loopGains[i] = expf(-6.907755f * delayInSamples / ((float)sampleRate * currentDecay));
            }
        }

        float g_hf = currentDamping * 0.6f;

        float x_low[8], x_mid[8], x_high[8];
        for (int i = 0; i < 8; ++i) {
            float delayInSamples = baseDelayLengths[i] * rateScale * modeScale;
            float readVal = fdnDelayLines[i].read(delayInSamples);
            crossovers[i].process(readVal, x_low[i], x_mid[i], x_high[i]);
        }

        float x_damped[8];
        for (int i = 0; i < 8; ++i) {
            float g_low = loopGains[i] * decayLow;
            float g_mid = loopGains[i] * decayMid;
            float g_high = loopGains[i] * decayHigh;

            if (g_low > 0.999f && !freeze) g_low = 0.999f;
            if (g_mid > 0.999f && !freeze) g_mid = 0.999f;
            if (g_high > 0.999f && !freeze) g_high = 0.999f;

            float high_filtered = fdnDampingFilters[i].process(x_high[i], g_hf);
            x_damped[i] = x_low[i] * g_low + x_mid[i] * g_mid + high_filtered * g_high;
        }

        /* Householder Feedback Matrix */
        float sum = 0.0f;
        for (int i = 0; i < 8; ++i) {
            sum += x_damped[i];
        }

        float factor = 0.25f;
        float x_feedback[8];
        for (int i = 0; i < 8; ++i) {
            x_feedback[i] = x_damped[i] - factor * sum;
        }

        /* Input distribution */
        float fdnInput[8];
        fdnInput[0] = diffL;
        fdnInput[1] = -diffL;
        fdnInput[2] = diffL;
        fdnInput[3] = -diffL;
        fdnInput[4] = diffR;
        fdnInput[5] = -diffR;
        fdnInput[6] = diffR;
        fdnInput[7] = -diffR;

        float thicknessScale = 1.0f + 0.3f * thickness;
        for (int i = 0; i < 8; ++i) {
            float writeVal = fdnInput[i] * thicknessScale + x_feedback[i] * loopGains[i];
            fdnDelayLines[i].write(writeVal);
        }

        float wetL = (x_damped[0] - x_damped[1] + x_damped[2] - x_damped[3]) * 0.5f;
        float wetR = (x_damped[4] - x_damped[5] + x_damped[6] - x_damped[7]) * 0.5f;

        /* Post EQ stage filtering */
        wetL = postEqLeftLow.processSample(wetL);
        wetL = postEqLeftMid.processSample(wetL);
        wetL = postEqLeftHigh.processSample(wetL);

        wetR = postEqRightLow.processSample(wetR);
        wetR = postEqRightMid.processSample(wetR);
        wetR = postEqRightHigh.processSample(wetR);

        /* Early/Late balance */
        float outWetL = (1.0f - distance) * earlyL + distance * wetL;
        float outWetR = (1.0f - distance) * earlyR + distance * wetR;

        /* Stereo Width mid/side adjust */
        float mid = (outWetL + outWetR) * 0.5f;
        float side = (outWetL - outWetR) * 0.5f;
        float adjustedSide = side * width;
        outWetL = mid + adjustedSide;
        outWetR = mid - adjustedSide;

        /* Dynamic Ducking */
        float inLevelDb = duckDetector.processDb((std::abs(inL) + std::abs(inR)) * 0.5f);
        float duckDb = 0.0f;
        if (inLevelDb > duckThreshold) {
            duckDb = (inLevelDb - duckThreshold) * (duckAmount / 30.0f);
            if (duckDb > duckAmount) duckDb = duckAmount;
        }
        float duckGain = powf(10.0f, -duckDb / 20.0f);
        outWetL *= duckGain;
        outWetR *= duckGain;

        /* Auto-Gating */
        float wetLevelDb = gateDetector.processDb((std::abs(outWetL) + std::abs(outWetR)) * 0.5f);
        if (wetLevelDb < gateThreshold) {
            gateTimerMs += (1000.0f / (float)sampleRate);
            if (gateTimerMs >= gateTime) {
                /* Plain conditional, not std::max -- windows.h's min/max
                 * macros (pulled in transitively via cuif/ring_buffer.h)
                 * clash with std::max/std::min unless NOMINMAX is defined
                 * before the windows.h include; avoiding them here is
                 * simpler than chasing include order. */
                float candidate = gateFade - 0.002f;
                gateFade = (candidate > 0.0f) ? candidate : 0.0f;
            }
        } else {
            gateTimerMs = 0.0f;
            float candidate = gateFade + 0.005f;
            gateFade = (candidate < 1.0f) ? candidate : 1.0f;
        }
        outWetL *= gateFade;
        outWetR *= gateFade;

        /* Global mix */
        leftChannel[sample] = (1.0f - dryWet) * inL + dryWet * outWetL;
        rightChannel[sample] = (1.0f - dryWet) * inR + dryWet * outWetR;

        /* Real-time FFT analyzers */
        if (fftAnalyzerLeft.pushSample(outWetL)) {
            /* Straight into the float[64] -- no per-frame std::vector (#47). */
            fftAnalyzerLeft.getMagnitudes(leftAnalyzerMagnitudes);
            leftAnalyzerDirty = true;
        }
        if (fftAnalyzerRight.pushSample(outWetR)) {
            /* Straight into the float[64] -- no per-frame std::vector (#47). */
            fftAnalyzerRight.getMagnitudes(rightAnalyzerMagnitudes);
            rightAnalyzerDirty = true;
        }
    }
}

bool ReverbCore::getParameterInfo(int index, ReverbParamMeta* outMeta) const {
    if (index < 0 || index >= kNumParams || !outMeta) return false;
    *outMeta = kReverbParams[index];
    return true;
}

float ReverbCore::getParameterValue(int index) const {
    if (index < 0 || index >= kNumParams) return 0.0f;
    return params[index];
}

void ReverbCore::setParameterValueDirect(int index, float value) {
    if (index < 0 || index >= kNumParams) return;
    params[index] = value;
}

bool ReverbCore::setParameterValue(int index, float value) {
    if (index < 0 || index >= kNumParams) return false;

    /* All-or-nothing write -- see PluginABI/examples/toy_gain/toy_gain.c
     * (#98/B1) and the follow-up issue #107 for why cuif_spsc_write()'s
     * partial-write behavior is unsafe for fixed-size message records. */
    ParamUpdateMessage msg;
    msg.index = index;
    msg.value = value;
    if (cuif_spsc_writable(&paramUpdateRingBuffer) < sizeof(msg)) return false;
    return cuif_spsc_write(&paramUpdateRingBuffer, (const unsigned char*)&msg, sizeof(msg)) == sizeof(msg);
}

/*
 * State blob format v1 (all integers little-endian, as written on x86/x64):
 *
 *   "LRVB"                      4-byte magic
 *   u16 version = 1
 *   u16 entryCount
 *   entryCount x { u8 idLen; char id[idLen]; f32 value; }
 *
 * Keyed on kReverbParams[].id, never on index -- the rule lpi.h states for
 * every LPI plugin -- so inserting or reordering parameters can't silently
 * load a preset's values into the wrong parameters. On load: unknown ids
 * are skipped (a newer build's preset loads in an older build), parameters
 * missing from the blob get their default (an older preset loads in a newer
 * build), values are clamped to the parameter's range, non-finite values
 * fall back to the default. The blob is fully validated before anything is
 * applied, so a truncated/corrupt blob leaves the current state untouched.
 *
 * Legacy: before this format existed getState() wrote the raw float array
 * (kLegacyParamCount floats, positional). A blob of exactly that size with
 * no magic is still accepted, mapped positionally to that original order.
 */
namespace {
const unsigned char kStateMagic[4] = { 'L', 'R', 'V', 'B' };
const uint16_t kStateVersion = 1;
const int kLegacyParamCount = 23; /* table size when the raw-array format was written */

float sanitizeParam(int index, float v) {
    const ReverbParamMeta& m = kReverbParams[index];
    if (!std::isfinite(v)) return m.defaultValue;
    if (v < m.minValue) return m.minValue;
    if (v > m.maxValue) return m.maxValue;
    return v;
}

int findParamById(const char* id, size_t len) {
    for (int i = 0; i < kNumParams; ++i) {
        if (std::strlen(kReverbParams[i].id) == len && std::memcmp(kReverbParams[i].id, id, len) == 0) return i;
    }
    return -1;
}
} // namespace

size_t ReverbCore::getState(void* buffer, size_t bufferSize) const {
    size_t required = sizeof(kStateMagic) + 2 * sizeof(uint16_t);
    for (int i = 0; i < kNumParams; ++i) {
        required += 1 + std::strlen(kReverbParams[i].id) + sizeof(float);
    }
    if (buffer == nullptr) return required;
    if (bufferSize < required) return 0;

    unsigned char* out = static_cast<unsigned char*>(buffer);
    std::memcpy(out, kStateMagic, sizeof(kStateMagic)); out += sizeof(kStateMagic);
    uint16_t version = kStateVersion, count = (uint16_t)kNumParams;
    std::memcpy(out, &version, sizeof(version)); out += sizeof(version);
    std::memcpy(out, &count, sizeof(count)); out += sizeof(count);
    for (int i = 0; i < kNumParams; ++i) {
        size_t idLen = std::strlen(kReverbParams[i].id);
        *out++ = (unsigned char)idLen;
        std::memcpy(out, kReverbParams[i].id, idLen); out += idLen;
        std::memcpy(out, &params[i], sizeof(float)); out += sizeof(float);
    }
    return required;
}

bool ReverbCore::setState(const void* data, size_t size) {
    if (!data) return false;
    const unsigned char* in = static_cast<const unsigned char*>(data);
    float loaded[kNumParams];

    if (size == sizeof(float) * kLegacyParamCount && std::memcmp(in, kStateMagic, sizeof(kStateMagic)) != 0) {
        for (int i = 0; i < kNumParams; ++i) loaded[i] = kReverbParams[i].defaultValue;
        for (int i = 0; i < kLegacyParamCount && i < kNumParams; ++i) {
            float v;
            std::memcpy(&v, in + i * sizeof(float), sizeof(float));
            loaded[i] = sanitizeParam(i, v);
        }
        std::memcpy(params, loaded, sizeof(params));
        return true;
    }

    const unsigned char* end = in + size;
    if (size < sizeof(kStateMagic) + 2 * sizeof(uint16_t)) return false;
    if (std::memcmp(in, kStateMagic, sizeof(kStateMagic)) != 0) return false;
    in += sizeof(kStateMagic);
    uint16_t version, count;
    std::memcpy(&version, in, sizeof(version)); in += sizeof(version);
    std::memcpy(&count, in, sizeof(count)); in += sizeof(count);
    if (version != kStateVersion) return false; /* a future v2 needs its own reader */

    for (int i = 0; i < kNumParams; ++i) loaded[i] = kReverbParams[i].defaultValue;
    for (uint16_t e = 0; e < count; ++e) {
        if (end - in < 1) return false;
        size_t idLen = *in++;
        if ((size_t)(end - in) < idLen + sizeof(float)) return false;
        const char* id = reinterpret_cast<const char*>(in); in += idLen;
        float v;
        std::memcpy(&v, in, sizeof(float)); in += sizeof(float);
        int index = findParamById(id, idLen);
        if (index >= 0) loaded[index] = sanitizeParam(index, v);
    }
    std::memcpy(params, loaded, sizeof(params));
    return true;
}

void ReverbCore::getLeftAnalyzerMagnitudes(float* out64) {
    if (out64) std::memcpy(out64, leftAnalyzerMagnitudes, sizeof(leftAnalyzerMagnitudes));
    leftAnalyzerDirty = false;
}

void ReverbCore::getRightAnalyzerMagnitudes(float* out64) {
    if (out64) std::memcpy(out64, rightAnalyzerMagnitudes, sizeof(rightAnalyzerMagnitudes));
    rightAnalyzerDirty = false;
}
