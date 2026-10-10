/*
 * Phaser DSP tests (#144): the platform's generic core checks plus the
 * Phaser's own: each all-pass stage has unity magnitude at 100 Hz, 1 kHz
 * and 10 kHz, N stages give N/2 notches on white noise at the predicted
 * frequencies, feedback 90% stays bounded, the stereo offset separates
 * the channels, mix 0 is bit-exact, and golden impulse responses.
 *
 *   dsp_tests                 run everything
 *   dsp_tests --regen-golden  rewrite test/golden/ after an INTENTIONAL DSP change
 */
#include "PhaserCore.h"
#include "loudio/testing/CoreTests.h"
#include "loudio/dsp/AllPassStage.h"
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

#ifndef LOUDIO_PHASER_GOLDEN_DIR
#define LOUDIO_PHASER_GOLDEN_DIR "test/golden"
#endif

namespace {

const char kStateMagic[4] = { 'P', 'H', 'S', 'R' };
const int kGoldenIrLength = 8192;
const double kGoldenSampleRate = 44100.0;
const double kFs = 48000.0;

void goldenTwelve(PhaserCore& c) {
    c.setParameterValueDirect(kPhaserStages, 3.0f);
    c.setParameterValueDirect(kPhaserRate, 3.0f);
    c.setParameterValueDirect(kPhaserDepth, 100.0f);
    c.setParameterValueDirect(kPhaserFeedback, -60.0f);
}

const loudio::testing::GoldenVariant<PhaserCore> kGoldenVariants[] = {
    { "default", nullptr },
    { "twelve", goldenTwelve },
};
const int kGoldenVariantCount = (int)(sizeof(kGoldenVariants) / sizeof(kGoldenVariants[0]));

float noise(unsigned& seed) {
    seed = seed * 1664525u + 1013904223u;
    return ((float)(seed >> 8) / 16777216.0f) * 2.0f - 1.0f;
}

void testPhaserAllPassIsUnityMagnitude() {
    std::cout << "Running testPhaserAllPassIsUnityMagnitude..." << std::endl;
    for (double hz : { 100.0, 1000.0, 10000.0 }) {
        loudio::AllPassStage stage;
        stage.setCornerFrequency(kFs, 1000.0f);
        double sumIn = 0.0, sumOut = 0.0;
        for (int n = 0; n < 96000; ++n) {
            float x = (float)std::sin(2.0 * M_PI * hz * (double)n / kFs);
            float y = stage.process(x);
            if (n >= 48000) { sumIn += (double)x * x; sumOut += (double)y * y; }
        }
        double db = 10.0 * std::log10(sumOut / sumIn);
        assert(std::abs(db) < 0.01);
    }
    std::cout << "  One stage (1 kHz corner): unity magnitude at 100 Hz, 1 kHz and 10 kHz within 0.01 dB. Pass." << std::endl;
}

const int kFftOrder = 14, kFftSize = 1 << kFftOrder; /* 2.93 Hz bins: the lowest notch (105 Hz) spans 14 of them */

/* Average power spectrum (dB) of the left output on -20 dBFS white
 * noise, Hann frames. */
std::vector<float> averageSpectrumDb(PhaserCore& core, int frames) {
    const int n = kFftSize;
    SimpleFFT fft(kFftOrder);
    std::vector<float> l((size_t)n), r((size_t)n), data((size_t)n * 2);
    std::vector<double> power((size_t)n / 2, 0.0);
    unsigned seed = 31;
    for (int f = -2; f < frames; ++f) {
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

/* Count the notches: contiguous bin runs more than `depthDb` below the
 * spectrum's top, between 50 Hz and 20 kHz, with their centre bins. The
 * spectrum is smoothed over 7 bins first, and runs separated by a gap
 * smaller than 12% of their frequency are one notch (a broad high notch
 * has ripple from the averaged noise that would otherwise split it). */
int countNotches(const std::vector<float>& raw, float depthDb, std::vector<int>& centres) {
    std::vector<float> db(raw.size());
    for (size_t k = 0; k < raw.size(); ++k) {
        float sum = 0.0f; int n = 0;
        for (int d = -3; d <= 3; ++d) { long j = (long)k + d; if (j >= 0 && j < (long)raw.size()) { sum += raw[(size_t)j]; ++n; } }
        db[k] = sum / (float)n;
    }
    const int lo = (int)(50.0 / (kFs / kFftSize)), hi = (int)(20000.0 / (kFs / kFftSize));
    float top = -1e9f;
    for (int k = lo; k < hi; ++k) top = std::max(top, db[(size_t)k]);
    std::vector<std::pair<int, int>> runs;
    bool inNotch = false;
    int start = 0;
    for (int k = lo; k <= hi; ++k) {
        bool below = k < hi && db[(size_t)k] < top - depthDb;
        if (below && !inNotch) { inNotch = true; start = k; }
        if (!below && inNotch) { inNotch = false; runs.push_back({ start, k - 1 }); }
    }
    std::vector<std::pair<int, int>> merged;
    for (const auto& r : runs) {
        if (!merged.empty() && (r.first - merged.back().second) < 0.12 * merged.back().second) merged.back().second = r.second;
        else merged.push_back(r);
    }
    centres.clear();
    for (const auto& r : merged) centres.push_back((r.first + r.second) / 2);
    return (int)merged.size();
}

void testPhaserStagesGiveNotches() {
    std::cout << "Running testPhaserStagesGiveNotches..." << std::endl;
    /* Depth 0 (static corner at 800 Hz), mix 50, no feedback: N stages ->
     * N/2 notches at 800 * tan((2k+1) pi / (2N)) Hz. */
    for (int option = 0; option < kPhaserNumStageOptions; ++option) {
        const int N = kPhaserStageCounts[option];
        PhaserCore core;
        core.setParameterValueDirect(kPhaserDepth, 0.0f);
        core.setParameterValueDirect(kPhaserCentre, 800.0f);
        core.setParameterValueDirect(kPhaserFeedback, 0.0f);
        core.setParameterValueDirect(kPhaserStages, (float)option);
        core.setParameterValueDirect(kPhaserMix, 50.0f);
        core.prepare(kFs, 4096);
        std::vector<float> db = averageSpectrumDb(core, 24);
        std::vector<int> centres;
        int notches = countNotches(db, 12.0f, centres);
        std::cout << "    " << N << " stages: " << notches << " notches at";
        for (int c : centres) std::cout << " " << (int)(c * kFs / kFftSize);
        std::cout << " Hz (predicted";
        for (int k = 0; k < N / 2; ++k) std::cout << " " << (int)(800.0 * std::tan((2.0 * k + 1.0) * M_PI / (2.0 * N)));
        std::cout << ")" << std::endl;
        assert(notches == N / 2);
        for (int k = 0; k < N / 2; ++k) {
            double predicted = 800.0 * std::tan((2.0 * k + 1.0) * M_PI / (2.0 * N));
            double measured = centres[(size_t)k] * kFs / kFftSize;
            assert(std::abs(measured - predicted) < std::max(15.0, 0.08 * predicted)); /* within 5 bins or 8% */
        }
    }
    std::cout << "  4 / 6 / 8 / 12 stages: exactly 2 / 3 / 4 / 6 notches, at the predicted frequencies. Pass." << std::endl;
}

void testPhaserFeedbackBoundedAndStereo() {
    std::cout << "Running testPhaserFeedbackBoundedAndStereo..." << std::endl;
    PhaserCore core;
    core.setParameterValueDirect(kPhaserFeedback, 90.0f);
    core.setParameterValueDirect(kPhaserDepth, 100.0f);
    core.setParameterValueDirect(kPhaserRate, 4.0f);
    core.setParameterValueDirect(kPhaserStages, 3.0f);
    core.setParameterValueDirect(kPhaserStereoPhase, 180.0f);
    core.prepare(kFs, 512);
    std::vector<float> l(512), r(512);
    unsigned seed = 5;
    float peak = 0.0f;
    double diff = 0.0;
    for (int b = 0; b < 400; ++b) {
        for (int i = 0; i < 512; ++i) { l[(size_t)i] = noise(seed); r[(size_t)i] = l[(size_t)i]; }
        core.process(l.data(), r.data(), 512);
        for (int i = 0; i < 512; ++i) {
            assert(std::isfinite(l[(size_t)i]) && std::isfinite(r[(size_t)i]));
            peak = std::max(peak, std::abs(l[(size_t)i]));
            diff += std::abs((double)l[(size_t)i] - r[(size_t)i]);
        }
    }
    assert(peak < 6.0f);
    assert(diff / (400.0 * 512.0) > 0.05); /* identical input, 180 degrees apart: clearly different outputs */

    /* Stereo phase 0 on identical input: identical outputs. */
    PhaserCore mono;
    mono.setParameterValueDirect(kPhaserStereoPhase, 0.0f);
    mono.prepare(kFs, 512);
    for (int i = 0; i < 512; ++i) { l[(size_t)i] = noise(seed); r[(size_t)i] = l[(size_t)i]; }
    mono.process(l.data(), r.data(), 512);
    for (int i = 0; i < 512; ++i) assert(l[(size_t)i] == r[(size_t)i]);
    std::cout << "  Feedback 90% with full sweep on noise: finite and bounded; stereo phase separates the channels, 0 keeps them identical. Pass." << std::endl;
}

void testPhaserMixZero() {
    std::cout << "Running testPhaserMixZero..." << std::endl;
    PhaserCore core;
    core.setParameterValueDirect(kPhaserMix, 0.0f);
    core.setParameterValueDirect(kPhaserFeedback, 80.0f);
    core.prepare(kFs, 4096);
    std::vector<float> inL(4096), inR(4096), l, r;
    unsigned seed = 9;
    for (int i = 0; i < 4096; ++i) { inL[(size_t)i] = noise(seed); inR[(size_t)i] = noise(seed); }
    l = inL; r = inR;
    core.process(l.data(), r.data(), 4096);
    for (int i = 0; i < 4096; ++i) { assert(l[(size_t)i] == inL[(size_t)i]); assert(r[(size_t)i] == inR[(size_t)i]); }
    std::cout << "  Mix 0 is bit-exact dry. Pass." << std::endl;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--regen-golden") {
        std::cout << "Regenerating golden impulse responses..." << std::endl;
        loudio::testing::regenerateGoldenImpulseResponses<PhaserCore>(LOUDIO_PHASER_GOLDEN_DIR, kGoldenVariants, kGoldenVariantCount, kGoldenIrLength, kGoldenSampleRate);
        return 0;
    }

    std::cout << "================================" << std::endl;
    std::cout << "Starting Loudio Phaser DSP Tests" << std::endl;
    std::cout << "================================" << std::endl;

    using namespace loudio::testing;
    testParamTableIndexIntegrity<PhaserCore>();
    testDefaultsMatchTable<PhaserCore>();
    testDirectSetAppliesImmediately<PhaserCore>(kPhaserRate, 2.0f);
    testQueuedSetAppliesAfterProcess<PhaserCore>(kPhaserRate, 2.0f);
    testBatchIsAllOrNothing<PhaserCore>(kPhaserRate, 2.0f);
    testStateRoundTrip<PhaserCore>(kStateMagic);
    testStateRejectsCorruptBlobs<PhaserCore>();
    testProcessPerformsNoHeapAllocations<PhaserCore>(44100.0, 512, 16, [](PhaserCore& core) {
        loudio::ParamUpdate u[3] = { { kPhaserStages, 3.0f }, { kPhaserCentre, 2000.0f }, { kPhaserFeedback, -50.0f } };
        core.setParameterValues(u, 3);
        core.setParameterValue(kPhaserDepth, 90.0f);
    });
    testPhaserAllPassIsUnityMagnitude();
    testPhaserStagesGiveNotches();
    testPhaserFeedbackBoundedAndStereo();
    testPhaserMixZero();
    testImpulseResponseMatchesGolden<PhaserCore>(LOUDIO_PHASER_GOLDEN_DIR, kGoldenVariants, kGoldenVariantCount, kGoldenIrLength, kGoldenSampleRate);

    std::cout << "All Phaser DSP tests passed successfully!" << std::endl;
    return 0;
}
