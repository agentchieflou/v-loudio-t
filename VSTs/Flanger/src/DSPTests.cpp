/*
 * Flanger DSP tests (#143): the platform's generic core checks plus the
 * Flanger's own: with modulation off the comb's notches sit at odd
 * multiples of 1/(2d) (SimpleFFT on white noise), feedback 90% stays
 * bounded, stereo phase 180 degrees puts the right channel's sweep half
 * a period behind the left, through-zero with depth 0 is a pure centre
 * delay, mix 0 is bit-exact dry, and golden impulse responses.
 *
 *   dsp_tests                 run everything
 *   dsp_tests --regen-golden  rewrite test/golden/ after an INTENTIONAL DSP change
 */
#include "FlangerCore.h"
#include "loudio/testing/CoreTests.h"
#include "loudio/dsp/Decibels.h"
#include "loudio/dsp/SimpleFFT.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

LOUDIO_DEFINE_ALLOCATION_HOOK();

#ifndef M_PI /* MSVC's <cmath> only defines it with _USE_MATH_DEFINES */
#define M_PI 3.14159265358979323846
#endif

#ifndef LOUDIO_FLANGER_GOLDEN_DIR
#define LOUDIO_FLANGER_GOLDEN_DIR "test/golden"
#endif

namespace {

const char kStateMagic[4] = { 'F', 'L', 'N', 'G' };
const int kGoldenIrLength = 8192;
const double kGoldenSampleRate = 44100.0;
const double kFs = 48000.0;

void goldenThroughZero(FlangerCore& c) {
    c.setParameterValueDirect(kFlangerRate, 2.0f);
    c.setParameterValueDirect(kFlangerDepth, 100.0f);
    c.setParameterValueDirect(kFlangerCentre, 1.0f);
    c.setParameterValueDirect(kFlangerFeedback, -60.0f);
    c.setParameterValueDirect(kFlangerThroughZero, 1.0f);
}

const loudio::testing::GoldenVariant<FlangerCore> kGoldenVariants[] = {
    { "default", nullptr },
    { "throughzero", goldenThroughZero },
};
const int kGoldenVariantCount = (int)(sizeof(kGoldenVariants) / sizeof(kGoldenVariants[0]));

FlangerCore makeStatic(float centreMs, float feedbackPercent, float mixPercent) {
    FlangerCore core;
    core.setParameterValueDirect(kFlangerDepth, 0.0f);
    core.setParameterValueDirect(kFlangerCentre, centreMs);
    core.setParameterValueDirect(kFlangerFeedback, feedbackPercent);
    core.setParameterValueDirect(kFlangerMix, mixPercent);
    core.prepare(kFs, 4096);
    return core;
}

float noise(unsigned& seed) {
    seed = seed * 1664525u + 1013904223u;
    return ((float)(seed >> 8) / 16777216.0f) * 2.0f - 1.0f;
}

/* Average power spectrum (dB per bin) of the left output on white noise,
 * 4096-point frames, Hann window, `frames` frames after a warm-up. */
std::vector<float> averageSpectrumDb(FlangerCore& core, int frames) {
    const int n = 4096;
    SimpleFFT fft(12);
    std::vector<float> l((size_t)n), r((size_t)n), data((size_t)n * 2);
    std::vector<double> power((size_t)n / 2, 0.0);
    unsigned seed = 77;
    for (int f = -2; f < frames; ++f) {
        /* -20 dBFS noise keeps the feedback path's soft clip transparent. */
        for (int i = 0; i < n; ++i) { l[(size_t)i] = 0.1f * noise(seed); r[(size_t)i] = l[(size_t)i]; }
        core.process(l.data(), r.data(), n);
        if (f < 0) continue;
        for (int i = 0; i < n; ++i) {
            float w = 0.5f - 0.5f * std::cos(2.0f * (float)M_PI * (float)i / (float)(n - 1));
            data[(size_t)(2 * i)] = l[(size_t)i] * w;
            data[(size_t)(2 * i + 1)] = 0.0f;
        }
        fft.performRealOnlyForwardTransform(data.data());
        for (int k = 0; k < n / 2; ++k) power[(size_t)k] += (double)data[(size_t)(2 * k)] * data[(size_t)(2 * k)] + (double)data[(size_t)(2 * k + 1)] * data[(size_t)(2 * k + 1)];
    }
    std::vector<float> db((size_t)n / 2);
    for (int k = 0; k < n / 2; ++k) db[(size_t)k] = 10.0f * std::log10((float)(power[(size_t)k] / frames) + 1e-20f);
    return db;
}

void testFlangerCombNotches() {
    std::cout << "Running testFlangerCombNotches..." << std::endl;
    /* Depth 0, centre 128 samples (2.6667 ms at 48 kHz), mix 50, no
     * feedback: H = (1 + z^-128) / 2, notches at odd multiples of
     * fs / 256 = 187.5 Hz, i.e. bins 16, 48, 80, ... of a 4096-point FFT,
     * peaks at bins 32, 64, ... */
    FlangerCore core = makeStatic(128.0f / 48.0f, 0.0f, 50.0f);
    std::vector<float> db = averageSpectrumDb(core, 64);
    for (int notch = 16; notch <= 400; notch += 32) {
        float peakBefore = db[(size_t)(notch - 16)], peakAfter = db[(size_t)(notch + 16)];
        float peak = 0.5f * (peakBefore + peakAfter);
        assert(peak - db[(size_t)notch] > 20.0f);
    }
    /* Negative feedback (-70%) moves the resonance to the former notches:
     * |0.5 (1 - 1/(1-0.7))| = 1.17 there vs 0.5 (1 + 1/1.7) = 0.79 at the
     * former peaks, i.e. the comb's shape flips by ~3.4 dB. */
    FlangerCore inv = makeStatic(128.0f / 48.0f, -70.0f, 50.0f);
    std::vector<float> dbInv = averageSpectrumDb(inv, 64);
    std::cout << "    negative-feedback flip: " << dbInv[16] - dbInv[32] << " dB (theory 3.4)" << std::endl;
    assert(dbInv[16] - dbInv[32] > 2.0f && dbInv[16] - dbInv[32] < 5.0f);
    std::cout << "  Static comb: notches at odd multiples of 187.5 Hz, >20 dB deep; negative feedback flips the comb's shape. Pass." << std::endl;
}

void testFlangerFeedbackBounded() {
    std::cout << "Running testFlangerFeedbackBounded..." << std::endl;
    FlangerCore core;
    core.setParameterValueDirect(kFlangerFeedback, 90.0f);
    core.setParameterValueDirect(kFlangerDepth, 100.0f);
    core.setParameterValueDirect(kFlangerRate, 5.0f);
    core.prepare(kFs, 512);
    std::vector<float> l(512), r(512);
    unsigned seed = 5;
    float peak = 0.0f;
    for (int b = 0; b < 400; ++b) { /* ~4 s */
        for (int i = 0; i < 512; ++i) { l[(size_t)i] = noise(seed); r[(size_t)i] = noise(seed); }
        core.process(l.data(), r.data(), 512);
        for (int i = 0; i < 512; ++i) { assert(std::isfinite(l[(size_t)i]) && std::isfinite(r[(size_t)i])); peak = std::max(peak, std::abs(l[(size_t)i])); }
    }
    assert(peak < 4.0f);
    std::cout << "  Feedback 90% with full-depth modulation on noise: finite and bounded. Pass." << std::endl;
}

/* Delay of the wet path on each channel over time: impulses every 2048
 * samples, the wet echo's centroid gives the fractional delay. */
void measureDelayTrajectory(FlangerCore& core, int impulses, std::vector<float>& dL, std::vector<float>& dR) {
    const int gap = 2048;
    std::vector<float> l((size_t)gap), r((size_t)gap);
    for (int k = 0; k < impulses; ++k) {
        std::fill(l.begin(), l.end(), 0.0f); std::fill(r.begin(), r.end(), 0.0f);
        l[0] = r[0] = 1.0f;
        core.process(l.data(), r.data(), gap);
        auto centroid = [&](const std::vector<float>& x) {
            double num = 0.0, den = 0.0;
            for (int i = 1; i < 1500; ++i) { num += (double)i * x[(size_t)i]; den += x[(size_t)i]; }
            return (float)(num / den);
        };
        dL.push_back(centroid(l));
        dR.push_back(centroid(r));
    }
}

void testFlangerStereoPhase() {
    std::cout << "Running testFlangerStereoPhase..." << std::endl;
    /* Rate 0.5 Hz, so the LFO period is 96 000 samples = 46.875 impulse
     * gaps... pick 2048 * 47 ~ a period; use rate fs / (2048 * 64) so one
     * period is exactly 64 impulses and 180 degrees is 32 impulses. */
    const float rate = (float)(kFs / (2048.0 * 64.0));
    auto run = [&](float stereoDeg, std::vector<float>& dL, std::vector<float>& dR) {
        FlangerCore core;
        core.setParameterValueDirect(kFlangerRate, rate);
        core.setParameterValueDirect(kFlangerDepth, 80.0f);
        core.setParameterValueDirect(kFlangerCentre, 5.0f);
        core.setParameterValueDirect(kFlangerFeedback, 0.0f);
        core.setParameterValueDirect(kFlangerStereoPhase, stereoDeg);
        core.setParameterValueDirect(kFlangerMix, 100.0f);
        core.prepare(kFs, 2048);
        measureDelayTrajectory(core, 128, dL, dR);
    };
    std::vector<float> dL, dR;
    run(180.0f, dL, dR);
    float sweep = *std::max_element(dL.begin(), dL.end()) - *std::min_element(dL.begin(), dL.end());
    assert(sweep > 300.0f); /* 5 ms centre, 80% depth: 240 +- 192 samples -> 384 of sweep */
    /* Right is left shifted by half a period (32 impulses), within a sample. */
    for (int k = 32; k < 96; ++k) assert(std::abs(dR[(size_t)k] - dL[(size_t)(k - 32)]) < 1.0f);
    /* And they differ at the same instant (except at the crossings). */
    float maxDiff = 0.0f;
    for (int k = 0; k < 64; ++k) maxDiff = std::max(maxDiff, std::abs(dR[(size_t)k] - dL[(size_t)k]));
    assert(maxDiff > 300.0f);
    /* Stereo phase 0: identical trajectories. */
    std::vector<float> sL, sR;
    run(0.0f, sL, sR);
    for (size_t k = 0; k < sL.size(); ++k) assert(std::abs(sL[k] - sR[k]) < 1e-3f);
    std::cout << "  Stereo phase 180: the right channel's sweep is the left's half a period later; 0 keeps them identical. Pass." << std::endl;
}

void testFlangerThroughZeroAndMix() {
    std::cout << "Running testFlangerThroughZeroAndMix..." << std::endl;
    /* Through-zero with depth 0: wet and dry are both delayed by the
     * centre (128 samples): the output is the input delayed by 128, no
     * comb at all. */
    FlangerCore tz = makeStatic(128.0f / 48.0f, 0.0f, 50.0f);
    tz.setParameterValueDirect(kFlangerThroughZero, 1.0f);
    tz.prepare(kFs, 4096);
    std::vector<float> inL(4096), inR(4096), l, r;
    unsigned seed = 9;
    for (int i = 0; i < 4096; ++i) { inL[(size_t)i] = noise(seed); inR[(size_t)i] = noise(seed); }
    l = inL; r = inR;
    tz.process(l.data(), r.data(), 4096);
    for (int i = 128; i < 4096; ++i) { assert(std::abs(l[(size_t)i] - inL[(size_t)(i - 128)]) < 1e-5f); assert(std::abs(r[(size_t)i] - inR[(size_t)(i - 128)]) < 1e-5f); }

    /* Mix 0 (through-zero off): bit-exact dry whatever the sweep does. */
    FlangerCore dry;
    dry.setParameterValueDirect(kFlangerMix, 0.0f);
    dry.setParameterValueDirect(kFlangerFeedback, 80.0f);
    dry.prepare(kFs, 4096);
    l = inL; r = inR;
    dry.process(l.data(), r.data(), 4096);
    for (int i = 0; i < 4096; ++i) { assert(l[(size_t)i] == inL[(size_t)i]); assert(r[(size_t)i] == inR[(size_t)i]); }
    std::cout << "  Through-zero with depth 0 is a pure centre delay; mix 0 is bit-exact dry. Pass." << std::endl;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--regen-golden") {
        std::cout << "Regenerating golden impulse responses..." << std::endl;
        loudio::testing::regenerateGoldenImpulseResponses<FlangerCore>(LOUDIO_FLANGER_GOLDEN_DIR, kGoldenVariants, kGoldenVariantCount, kGoldenIrLength, kGoldenSampleRate);
        return 0;
    }

    std::cout << "=================================" << std::endl;
    std::cout << "Starting Loudio Flanger DSP Tests" << std::endl;
    std::cout << "=================================" << std::endl;

    using namespace loudio::testing;
    testParamTableIndexIntegrity<FlangerCore>();
    testDefaultsMatchTable<FlangerCore>();
    testDirectSetAppliesImmediately<FlangerCore>(kFlangerRate, 2.0f);
    testQueuedSetAppliesAfterProcess<FlangerCore>(kFlangerRate, 2.0f);
    testBatchIsAllOrNothing<FlangerCore>(kFlangerRate, 2.0f);
    testStateRoundTrip<FlangerCore>(kStateMagic);
    testStateRejectsCorruptBlobs<FlangerCore>();
    testProcessPerformsNoHeapAllocations<FlangerCore>(44100.0, 512, 16, [](FlangerCore& core) {
        loudio::ParamUpdate u[3] = { { kFlangerCentre, 8.0f }, { kFlangerThroughZero, 1.0f }, { kFlangerFeedback, -50.0f } };
        core.setParameterValues(u, 3);
        core.setParameterValue(kFlangerDepth, 90.0f);
    });
    testFlangerCombNotches();
    testFlangerFeedbackBounded();
    testFlangerStereoPhase();
    testFlangerThroughZeroAndMix();
    testImpulseResponseMatchesGolden<FlangerCore>(LOUDIO_FLANGER_GOLDEN_DIR, kGoldenVariants, kGoldenVariantCount, kGoldenIrLength, kGoldenSampleRate);

    std::cout << "All Flanger DSP tests passed successfully!" << std::endl;
    return 0;
}
