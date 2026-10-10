/*
 * Glue DSP tests (#140): the platform's generic core checks plus the
 * Glue's own: the feedback loop is stable on a 0 dBFS square for 10 s
 * (bounded, no DC drift, no limit cycle), the feedback gain computer
 * gives the switch's ratio, the timing switches and auto release behave,
 * the tanh VCA's aliasing products sit below -60 dB thanks to the 4x
 * oversampling, mix 0 nulls against the (latency-delayed) input, and
 * golden impulse responses including one at ratio 4.
 *
 *   dsp_tests                 run everything
 *   dsp_tests --regen-golden  rewrite test/golden/ after an INTENTIONAL DSP change
 */
#include "GlueCore.h"
#include "loudio/testing/CoreTests.h"
#include "loudio/dsp/Decibels.h"

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

#ifndef LOUDIO_GLUE_GOLDEN_DIR
#define LOUDIO_GLUE_GOLDEN_DIR "test/golden"
#endif

namespace {

const char kStateMagic[4] = { 'G', 'L', 'U', 'E' };
const int kGoldenIrLength = 8192;
const double kGoldenSampleRate = 44100.0;
const double kFs = 48000.0;

void goldenRatio4(GlueCore& c) {
    c.setParameterValueDirect(kGlueThreshold, -30.0f);
    c.setParameterValueDirect(kGlueRatio, 1.0f); /* 4:1 */
    c.setParameterValueDirect(kGlueAttack, 0.0f);
    c.setParameterValueDirect(kGlueRelease, 0.0f);
    c.setParameterValueDirect(kGlueMakeup, 6.0f);
}
void goldenRatio10Mix(GlueCore& c) {
    c.setParameterValueDirect(kGlueThreshold, -40.0f);
    c.setParameterValueDirect(kGlueRatio, 2.0f); /* 10:1 */
    c.setParameterValueDirect(kGlueMix, 50.0f);
    c.setParameterValueDirect(kGlueScHpf, 100.0f);
}

const loudio::testing::GoldenVariant<GlueCore> kGoldenVariants[] = {
    { "default", nullptr },
    { "ratio4", goldenRatio4 },
    { "ratio10mix", goldenRatio10Mix },
};
const int kGoldenVariantCount = (int)(sizeof(kGoldenVariants) / sizeof(kGoldenVariants[0]));

GlueCore makeGlue(float thresholdDb, int ratioIndex, int attackIndex, int releaseIndex, int maxBlock = 512) {
    GlueCore core;
    core.setParameterValueDirect(kGlueThreshold, thresholdDb);
    core.setParameterValueDirect(kGlueRatio, (float)ratioIndex);
    core.setParameterValueDirect(kGlueAttack, (float)attackIndex);
    core.setParameterValueDirect(kGlueRelease, (float)releaseIndex);
    core.prepare(kFs, maxBlock);
    return core;
}

/* Peak output amplitude in dBFS over the last 100 ms of a one-second sine. */
double measureSineOutDb(GlueCore& core, double hz, double inDbfs) {
    const int n = (int)kFs, block = 480;
    const double amp = std::pow(10.0, inDbfs / 20.0);
    std::vector<float> l((size_t)block), r((size_t)block);
    float peak = 0.0f;
    for (int start = 0; start + block <= n; start += block) {
        for (int i = 0; i < block; ++i) l[(size_t)i] = r[(size_t)i] = (float)(amp * std::sin(2.0 * M_PI * hz * (double)(start + i) / kFs));
        core.process(l.data(), r.data(), block);
        if (start >= n - 4800) for (int i = 0; i < block; ++i) peak = std::max(peak, std::abs(l[(size_t)i]));
    }
    return 20.0 * std::log10((double)peak);
}

void testGlueFeedbackLoopIsStable() {
    std::cout << "Running testGlueFeedbackLoopIsStable..." << std::endl;
    /* The #140 acceptance test: a 0 dBFS square (100 Hz) for 10 s at
     * threshold -20, 10:1, fastest attack and release. Bounded (the tanh
     * rail is 2.0 so nothing can exceed it, but the compressed output
     * must sit far below), finite, no DC drift, and no limit cycle: the
     * applied gain reduction is steady over the last second. */
    GlueCore core = makeGlue(-20.0f, 2, 0, 0);
    const int block = 480, blocks = 1000; /* 10 s */
    std::vector<float> l((size_t)block), r((size_t)block);
    double sumLast = 0.0, countLast = 0.0;
    float peakLast = 0.0f, grMin = 1e9f, grMax = -1e9f;
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < block; ++i) {
            int n = b * block + i;
            l[(size_t)i] = r[(size_t)i] = ((n / 240) % 2 == 0) ? 1.0f : -1.0f;
        }
        core.process(l.data(), r.data(), block);
        for (int i = 0; i < block; ++i) assert(std::isfinite(l[(size_t)i]) && std::isfinite(r[(size_t)i]));
        if (b >= blocks - 100) {
            for (int i = 0; i < block; ++i) { sumLast += l[(size_t)i]; countLast += 1.0; peakLast = std::max(peakLast, std::abs(l[(size_t)i])); }
            grMin = std::min(grMin, core.meterGainReductionDb());
            grMax = std::max(grMax, core.meterGainReductionDb());
        }
    }
    assert(peakLast < 0.5f);                       /* 0 dBFS in, well below -6 dBFS out */
    assert(std::abs(sumLast / countLast) < 1e-3);  /* no DC drift */
    assert(grMax - grMin < 0.05f);                 /* no limit cycle: steady reduction */
    assert(grMax > 10.0f);                         /* and it is compressing hard */
    std::cout << "  10 s of a 0 dBFS square: finite, bounded, no DC drift, gain reduction steady within 0.05 dB. Pass." << std::endl;
}

void testGlueFeedbackRatio() {
    std::cout << "Running testGlueFeedbackRatio..." << std::endl;
    /* Feedback gain computer: output level settles where
     * L_in - (R-1)(L_out - T) = L_out, i.e. L_out = T + (L_in - T) / R.
     * -10 dBFS in, threshold -30: 4:1 -> -25, 2:1 -> -20, 10:1 -> -28.
     * The tanh rail (4.0) is negligible at these levels. */
    const double expected[] = { -20.0, -25.0, -28.0 };
    for (int ratio = 0; ratio < kGlueNumRatios; ++ratio) {
        GlueCore core = makeGlue(-30.0f, ratio, 0, 0);
        double out = measureSineOutDb(core, 1000.0, -10.0);
        assert(std::abs(out - expected[ratio]) < 0.3);
    }
    /* Below the threshold: untouched (to the tanh's -0.01 dB at -30 dBFS). */
    GlueCore quiet = makeGlue(-20.0f, 1, 0, 0);
    assert(std::abs(measureSineOutDb(quiet, 1000.0, -30.0) + 30.0) < 0.05);
    std::cout << "  2:1 / 4:1 / 10:1 switch positions give the feedback ratios within 0.3 dB; below threshold untouched. Pass." << std::endl;
}

/* Runs 1 ms DC blocks and appends the applied gain reduction at each block end. */
void runDc(GlueCore& core, float level, int blocks, std::vector<float>& trace) {
    std::vector<float> l(48), r(48);
    for (int b = 0; b < blocks; ++b) {
        std::fill(l.begin(), l.end(), level);
        std::fill(r.begin(), r.end(), level);
        core.process(l.data(), r.data(), 48);
        trace.push_back(core.currentGainReductionDb());
    }
}

void testGlueTimingSwitches() {
    std::cout << "Running testGlueTimingSwitches..." << std::endl;
    /* 10 ms attack (switch 4), 0.1 s release (switch 0), 10:1 at -20:
     * 0 dBFS DC settles at 18 dB of reduction (feedback: 9 * (L_out + 20)
     * = 20 - L_out -> L_out = -18). In a feedback loop the attack is
     * sharpened by the loop gain (R - 1 = 9): the 10 ms switch gets most
     * of the way in 2 ms (the hardware does the same: its attack times
     * shorten at higher ratios). Release runs open-loop once the output
     * is under the threshold, so it IS the switch's one-pole: 36.8% left
     * after 100 ms. (DC passes the oversampler, with 1 ms of latency.) */
    GlueCore core = makeGlue(-20.0f, 2, 4, 0);
    std::vector<float> up;
    runDc(core, 1.0f, 200, up);
    assert(up[2] > 0.5f * 18.0f && up[2] < 0.98f * 18.0f);
    assert(up[10] > 0.95f * 18.0f);
    assert(std::abs(up[199] - 18.0f) < 0.3f);
    std::vector<float> down;
    runDc(core, 0.1f, 300, down);
    assert(std::abs(down[100] / 18.0f - 0.368f) < 0.03f);
    assert(down[299] < 0.06f * 18.0f);
    for (size_t i = 1; i < down.size(); ++i) assert(down[i] <= down[i - 1] + 1e-3f);

    /* Auto release: after a SHORT burst (20 ms) the reduction is mostly
     * gone 200 ms later (fast path); after a LONG one (3 s) the slow
     * follower has charged and 200 ms later most of it is still there. */
    auto leftAfter = [](int burstBlocks) {
        GlueCore c = makeGlue(-20.0f, 2, 0, kGlueReleaseAuto);
        std::vector<float> t;
        runDc(c, 1.0f, burstBlocks, t);
        float held = t.back();
        runDc(c, 0.1f, 200, t);
        return t.back() / held;
    };
    assert(leftAfter(20) < 0.25f);
    assert(leftAfter(3000) > 0.5f);
    std::cout << "  Attack sharpened by the loop gain, release is the switch's one-pole (37% at 100 ms); auto release is program-dependent. Pass." << std::endl;
}

/* Goertzel magnitude at `hz` over a Hann-windowed buffer. */
double goertzelDb(const std::vector<float>& x, double hz) {
    const int n = (int)x.size();
    const double w = 2.0 * M_PI * hz / kFs;
    const double coeff = 2.0 * std::cos(w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; ++i) {
        double win = 0.5 - 0.5 * std::cos(2.0 * M_PI * (double)i / (double)(n - 1));
        s0 = (double)x[(size_t)i] * win + coeff * s1 - s2;
        s2 = s1; s1 = s0;
    }
    double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return 10.0 * std::log10(power + 1e-30);
}

void testGlueSaturationAliasingIsSuppressed() {
    std::cout << "Running testGlueSaturationAliasingIsSuppressed..." << std::endl;
    /* A 19 kHz sine at 0 dBFS into the tanh rail (threshold 0 so the VCA
     * is at unity and only the rail bends it): the 3rd harmonic at 57 kHz
     * (-46 dB) would alias to 9 kHz at 48 kHz without oversampling. At 4x it is
     * generated at 57 kHz and removed by the downsampler: the 9 kHz
     * product is below -60 dB relative to the fundamental. */
    GlueCore core = makeGlue(0.0f, 0, 0, 0);
    const int block = 480, blocks = 100;
    std::vector<float> l((size_t)block), r((size_t)block), out;
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < block; ++i) l[(size_t)i] = r[(size_t)i] = (float)std::sin(2.0 * M_PI * 19000.0 * (double)(b * block + i) / kFs);
        core.process(l.data(), r.data(), block);
        if (b >= blocks / 2) out.insert(out.end(), l.begin(), l.end());
    }
    double fundamental = goertzelDb(out, 19000.0), alias = goertzelDb(out, 9000.0);
    assert(alias - fundamental < -60.0);
    /* The rail does bend it: the 3rd harmonic of a 0 dBFS 1 kHz tone
     * (at 3 kHz, in band) is there, around -46 dB (u^2/12 for u = 1/4). */
    GlueCore ref = makeGlue(0.0f, 0, 0, 0);
    out.clear();
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < block; ++i) l[(size_t)i] = r[(size_t)i] = (float)std::sin(2.0 * M_PI * 1000.0 * (double)(b * block + i) / kFs);
        ref.process(l.data(), r.data(), block);
        if (b >= blocks / 2) out.insert(out.end(), l.begin(), l.end());
    }
    double h3 = goertzelDb(out, 3000.0) - goertzelDb(out, 1000.0);
    assert(h3 > -52.0 && h3 < -40.0);
    std::cout << "  19 kHz at 0 dBFS: the 9 kHz alias is >60 dB down; the rail's 3rd harmonic on 1 kHz sits around -46 dB. Pass." << std::endl;
}

void testGlueLatencyAndMix() {
    std::cout << "Running testGlueLatencyAndMix..." << std::endl;
    GlueCore core = makeGlue(0.0f, 0, 0, 0);
    assert(core.latencySamples() == 48);
    /* Mix 0: the output is the dry input delayed by the latency, exactly. */
    core.setParameterValueDirect(kGlueMix, 0.0f);
    core.setParameterValueDirect(kGlueThreshold, -40.0f);
    core.setParameterValueDirect(kGlueRatio, 2.0f);
    core.prepare(kFs, 512);
    std::vector<float> l(4096), r(4096), inL(4096), inR(4096);
    unsigned seed = 11;
    for (int i = 0; i < 4096; ++i) {
        seed = seed * 1664525u + 1013904223u;
        inL[(size_t)i] = l[(size_t)i] = ((float)(seed >> 8) / 16777216.0f) * 2.0f - 1.0f;
        inR[(size_t)i] = r[(size_t)i] = -inL[(size_t)i] * 0.7f;
    }
    core.process(l.data(), r.data(), 4096);
    assert(core.meterGainReductionDb() > 10.0f);
    for (int i = 48; i < 4096; ++i) {
        assert(l[(size_t)i] == inL[(size_t)(i - 48)]);
        assert(r[(size_t)i] == inR[(size_t)(i - 48)]);
    }
    /* Mix 100 at threshold 0, 2:1, a -40 dBFS sine: the wet path is a
     * 1e-4 passthrough of the delayed input (oversampler round trip). */
    GlueCore wet = makeGlue(0.0f, 0, 0, 0);
    std::vector<float> a(4800), b(4800);
    float worst = 0.0f;
    for (int i = 0; i < 4800; ++i) a[(size_t)i] = b[(size_t)i] = 0.01f * (float)std::sin(2.0 * M_PI * 1000.0 * (double)i / kFs);
    wet.process(a.data(), b.data(), 4800);
    for (int i = 1000; i < 4800; ++i) worst = std::max(worst, std::abs(a[(size_t)i] - 0.01f * (float)std::sin(2.0 * M_PI * 1000.0 * (double)(i - 48) / kFs)));
    assert(worst < 1e-5f);
    std::cout << "  Latency 48 (the oversampler); mix 0 is bit-exact delayed dry; the wet path passes a quiet signal within 1e-5. Pass." << std::endl;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--regen-golden") {
        std::cout << "Regenerating golden impulse responses..." << std::endl;
        loudio::testing::regenerateGoldenImpulseResponses<GlueCore>(LOUDIO_GLUE_GOLDEN_DIR, kGoldenVariants, kGoldenVariantCount, kGoldenIrLength, kGoldenSampleRate);
        return 0;
    }

    std::cout << "==============================" << std::endl;
    std::cout << "Starting Loudio Glue DSP Tests" << std::endl;
    std::cout << "==============================" << std::endl;

    using namespace loudio::testing;
    testParamTableIndexIntegrity<GlueCore>();
    testDefaultsMatchTable<GlueCore>();
    testDirectSetAppliesImmediately<GlueCore>(kGlueThreshold, -20.0f);
    testQueuedSetAppliesAfterProcess<GlueCore>(kGlueThreshold, -20.0f);
    testBatchIsAllOrNothing<GlueCore>(kGlueThreshold, -20.0f);
    testStateRoundTrip<GlueCore>(kStateMagic);
    testStateRejectsCorruptBlobs<GlueCore>();
    testProcessPerformsNoHeapAllocations<GlueCore>(44100.0, 512, 16, [](GlueCore& core) {
        loudio::ParamUpdate u[3] = { { kGlueThreshold, -30.0f }, { kGlueRelease, 2.0f }, { kGlueScHpf, 80.0f } };
        core.setParameterValues(u, 3);
        core.setParameterValue(kGlueMix, 60.0f);
    });
    testGlueFeedbackLoopIsStable();
    testGlueFeedbackRatio();
    testGlueTimingSwitches();
    testGlueSaturationAliasingIsSuppressed();
    testGlueLatencyAndMix();
    testImpulseResponseMatchesGolden<GlueCore>(LOUDIO_GLUE_GOLDEN_DIR, kGoldenVariants, kGoldenVariantCount, kGoldenIrLength, kGoldenSampleRate);

    std::cout << "All Glue DSP tests passed successfully!" << std::endl;
    return 0;
}
