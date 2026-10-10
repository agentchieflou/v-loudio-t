/*
 * Delay DSP tests (#142): the platform's generic core checks plus the
 * Delay's own: echoes at exactly N samples, each exactly -feedback dB
 * below the last (and a fractional time interpolated with no dropouts),
 * ping-pong cross-feed, tempo sync (1/4 at 120 BPM = 500 ms, dotted and
 * triplet, tempo changes), click-free time glides, the feedback filters,
 * mix 0 bit-exact, and golden impulse responses.
 *
 *   dsp_tests                 run everything
 *   dsp_tests --regen-golden  rewrite test/golden/ after an INTENTIONAL DSP change
 */
#include "DelayCore.h"
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

#ifndef LOUDIO_DELAY_GOLDEN_DIR
#define LOUDIO_DELAY_GOLDEN_DIR "test/golden"
#endif

namespace {

const char kStateMagic[4] = { 'D', 'L', 'A', 'Y' };
const int kGoldenIrLength = 8192;
const double kGoldenSampleRate = 44100.0;
const double kFs = 48000.0;

/* Goldens use short times so the 8192-sample IR holds several repeats. */
void goldenShort(DelayCore& c) {
    c.setParameterValueDirect(kDelayTimeL, 23.0f);
    c.setParameterValueDirect(kDelayTimeR, 31.0f);
    c.setParameterValueDirect(kDelayFeedback, 60.0f);
    c.setParameterValueDirect(kDelayMix, 100.0f);
}
void goldenPingPongMod(DelayCore& c) {
    c.setParameterValueDirect(kDelayTimeL, 17.0f);
    c.setParameterValueDirect(kDelayTimeR, 17.0f);
    c.setParameterValueDirect(kDelayFeedback, 70.0f);
    c.setParameterValueDirect(kDelayCrossFeed, 100.0f);
    c.setParameterValueDirect(kDelayModRate, 3.0f);
    c.setParameterValueDirect(kDelayModDepth, 2.0f);
    c.setParameterValueDirect(kDelayLowCut, 300.0f);
    c.setParameterValueDirect(kDelayHighCut, 3000.0f);
    c.setParameterValueDirect(kDelayMix, 50.0f);
}

const loudio::testing::GoldenVariant<DelayCore> kGoldenVariants[] = {
    { "default", nullptr },
    { "short", goldenShort },
    { "pingpongmod", goldenPingPongMod },
};
const int kGoldenVariantCount = (int)(sizeof(kGoldenVariants) / sizeof(kGoldenVariants[0]));

/* A core with the loop "clean": no filters (cuts at the range ends),
 * no modulation, 100% wet, so echoes can be measured sample-exactly. */
DelayCore makeClean(float timeMsL, float timeMsR, float feedbackPercent, float crossPercent = 0.0f) {
    DelayCore core;
    core.setParameterValueDirect(kDelayTimeL, timeMsL);
    core.setParameterValueDirect(kDelayTimeR, timeMsR);
    core.setParameterValueDirect(kDelayFeedback, feedbackPercent);
    core.setParameterValueDirect(kDelayCrossFeed, crossPercent);
    core.setParameterValueDirect(kDelayLowCut, 20.0f);
    core.setParameterValueDirect(kDelayHighCut, 20000.0f);
    core.setParameterValueDirect(kDelayModDepth, 0.0f);
    core.setParameterValueDirect(kDelayMix, 100.0f);
    core.prepare(kFs, 512);
    return core;
}

/* Impulse on the given side(s), returns both channels over `length` samples. */
void impulseResponse(DelayCore& core, bool left, bool right, int length, std::vector<float>& l, std::vector<float>& r) {
    l.assign((size_t)length, 0.0f);
    r.assign((size_t)length, 0.0f);
    if (left) l[0] = 1.0f;
    if (right) r[0] = 1.0f;
    for (int start = 0; start < length; start += 512) core.process(l.data() + start, r.data() + start, std::min(512, length - start));
}

void testDelayEchoesAreSampleExact() {
    std::cout << "Running testDelayEchoesAreSampleExact..." << std::endl;
    /* 500 ms = 24 000 samples at 48 kHz, feedback 50%, the loop filters
     * at their off positions: echoes at exactly 24 000, 48 000, 72 000.
     * The feedback is soft clipped (tanh), so a full-scale impulse's
     * repeats follow the clip's iterate (1, tanh(0.5) = 0.462, ...);
     * a quiet impulse sees a transparent clip and exact -6.02 dB steps. */
    DelayCore core = makeClean(500.0f, 500.0f, 50.0f);
    std::vector<float> l, r;
    impulseResponse(core, true, true, 100000, l, r);
    const int period = 24000;
    float expected = 1.0f;
    for (int k = 1; k <= 4; ++k) {
        int at = k * period;
        assert(std::abs(l[(size_t)at] - expected) < 1e-4f);
        /* nothing in between: the samples around each echo are silent */
        for (int d = 1; d < 200; ++d) { assert(l[(size_t)(at - d)] == 0.0f); assert(l[(size_t)(at + d)] == 0.0f); }
        expected = std::tanh(0.5f * expected); /* the soft-clipped feedback of this repeat */
    }
    /* With a small signal the clip is transparent and each repeat is
     * exactly -6.02 dB (feedback 50%) below the last: a 0.01 impulse. */
    for (int k = 2; k <= 4; ++k) {
        DelayCore q = makeClean(500.0f, 500.0f, 50.0f);
        std::vector<float> a((size_t)(k * period + 1), 0.0f), b((size_t)(k * period + 1), 0.0f);
        a[0] = b[0] = 0.01f;
        for (int start = 0; start < (int)a.size(); start += 512) q.process(a.data() + start, b.data() + start, std::min(512, (int)a.size() - start));
        float ratioDb = loudio::gainToDb(a[(size_t)(k * period)]) - loudio::gainToDb(a[(size_t)((k - 1) * period)]);
        assert(std::abs(ratioDb + 6.02f) < 0.01f);
    }

    /* Fractional time (20.51 ms = 984.48 samples): the echo is
     * interpolated between 984 and 985 with no dropout (#112) and the
     * energy is all there. */
    DelayCore frac = makeClean(20.51f, 20.51f, 0.0f);
    impulseResponse(frac, true, true, 4000, l, r);
    float sum = 0.0f;
    for (int i = 0; i < 4000; ++i) sum += l[(size_t)i];
    assert(std::abs(sum - 1.0f) < 0.02f);
    assert(l[984] > 0.2f && l[985] > 0.2f); /* both neighbours carry it */
    for (int i = 0; i < 980; ++i) assert(std::abs(l[(size_t)i]) < 1e-3f);

    std::cout << "  Echoes at exactly 24 000 n with silence between; each -6.02 dB below the last (soft clip transparent); a 20.51 ms time is interpolated with nothing dropped. Pass." << std::endl;
}

void testDelayPingPong() {
    std::cout << "Running testDelayPingPong..." << std::endl;
    /* Cross-feed 100%, same time both sides: a left-only impulse comes
     * back on the LEFT first (its own tap), then feeds the RIGHT line
     * only, so the second repeat is on the right, the third on the left. */
    DelayCore core = makeClean(10.0f, 10.0f, 50.0f, 100.0f);
    std::vector<float> l, r;
    impulseResponse(core, true, false, 4000, l, r);
    const int p = 480;
    /* Levels follow the soft clip's iterate: 1, tanh(0.5) = 0.462, tanh(0.231) = 0.227. */
    assert(l[p] > 0.98f && std::abs(r[p]) < 1e-3f);          /* 1st: left */
    assert(r[2 * p] > 0.45f && std::abs(l[2 * p]) < 1e-3f);  /* 2nd: right */
    assert(l[3 * p] > 0.22f && std::abs(r[3 * p]) < 1e-3f);  /* 3rd: left */
    /* Cross-feed 0: dual mono, everything stays left. */
    DelayCore mono = makeClean(10.0f, 10.0f, 50.0f, 0.0f);
    impulseResponse(mono, true, false, 4000, l, r);
    for (int i = 0; i < 4000; ++i) assert(r[(size_t)i] == 0.0f);
    assert(l[2 * p] > 0.45f);
    std::cout << "  Cross-feed 100 hops sides on every repeat; 0 keeps the sides apart. Pass." << std::endl;
}

void testDelayTempoSync() {
    std::cout << "Running testDelayTempoSync..." << std::endl;
    DelayCore core = makeClean(100.0f, 100.0f, 0.0f);
    core.setParameterValueDirect(kDelaySync, 1.0f);
    core.setParameterValueDirect(kDelayDivisionL, 8.0f); /* 1/4 */
    core.setParameterValueDirect(kDelayDivisionR, 6.0f); /* 1/8 dotted */
    loudio::TransportInfo t;
    t.tempoBpm = 120.0;
    t.flags = loudio::kTransportTempoValid | loudio::kTransportPlaying;
    core.setTransport(t);
    core.prepare(kFs, 512);
    assert(std::abs(core.effectiveTimeMsL() - 500.0f) < 1e-3f);
    assert(std::abs(core.effectiveTimeMsR() - 375.0f) < 1e-3f);
    std::vector<float> l, r;
    impulseResponse(core, true, true, 30000, l, r);
    assert(l[24000] > 0.99f && r[18000] > 0.99f); /* 500 ms and 375 ms exactly */

    /* Triplet: 1/8T at 120 = 166.67 ms = 8000 samples. */
    core.setParameterValueDirect(kDelayDivisionL, 4.0f);
    core.prepare(kFs, 512);
    assert(std::abs(core.effectiveTimeMsL() - 166.6667f) < 1e-2f);

    /* A tempo change moves the time (through the glide). */
    t.tempoBpm = 100.0;
    core.setTransport(t);
    std::vector<float> a(512, 0.0f), b(512, 0.0f);
    for (int blk = 0; blk < 10; ++blk) core.process(a.data(), b.data(), 512);
    assert(std::abs(core.effectiveTimeMsL() - 200.0f) < 1e-2f);
    /* Sync off: back to the ms parameter. */
    core.setParameterValueDirect(kDelaySync, 0.0f);
    core.process(a.data(), b.data(), 512);
    assert(core.effectiveTimeMsL() == 100.0f);
    std::cout << "  120 BPM: 1/4 = 500 ms, 1/8. = 375 ms, 1/8T = 166.67 ms, sample-exact; tempo changes and sync off follow. Pass." << std::endl;
}

void testDelayTimeGlideIsClickFree() {
    std::cout << "Running testDelayTimeGlideIsClickFree..." << std::endl;
    /* A 200 Hz sine, time jumping 100 -> 400 ms through the RT queue:
     * the delay ramps over 20 ms (a pitch slide), so no sample step may
     * exceed what a sine shifted by up to 15 samples per sample can do.
     * The output stays bounded and settles at the new time. */
    DelayCore core = makeClean(100.0f, 100.0f, 0.0f);
    std::vector<float> l(480), r(480);
    float prev = 0.0f, maxStep = 0.0f;
    int n = 0;
    auto run = [&](int blocks) {
        for (int b = 0; b < blocks; ++b) {
            for (int i = 0; i < 480; ++i) { l[(size_t)i] = r[(size_t)i] = (float)std::sin(2.0 * M_PI * 200.0 * (double)n / kFs); ++n; }
            core.process(l.data(), r.data(), 480);
            for (int i = 0; i < 480; ++i) { maxStep = std::max(maxStep, std::abs(l[(size_t)i] - prev)); prev = l[(size_t)i]; }
        }
    };
    run(50);
    maxStep = 0.0f;
    assert(core.setParameterValue(kDelayTimeL, 400.0f));
    run(100);
    /* The glide covers 300 ms of delay in 20 ms: 15 samples of delay per
     * sample, i.e. the read point moves 16 samples per output sample. A
     * 200 Hz sine advances 2 pi 200 / 48000 per sample: the bound is 16 of
     * those. Without the ramp the jump would be a full-scale discontinuity. */
    const float slope = 2.0f * (float)M_PI * 200.0f / 48000.0f;
    assert(maxStep < 16.5f * slope);
    assert(maxStep > 0.5f * slope); /* the slide really happened */
    assert(core.effectiveTimeMsL() == 400.0f);
    std::cout << "  A 100 -> 400 ms jump slides over 20 ms with every sample step within the glide's bound. Pass." << std::endl;
}

void testDelayFeedbackFiltersAndMix() {
    std::cout << "Running testDelayFeedbackFiltersAndMix..." << std::endl;
    /* High cut 2 kHz inside the loop: a 10 kHz tone's second repeat is
     * much quieter than a 200 Hz tone's. Measured on the wet output via
     * the second echo of a 1-cycle burst would be fiddly; instead compare
     * steady-state wet levels of long tones at feedback 50%, where the
     * loop gain at 10 kHz is cut by the filter each pass. */
    auto steadyWetDb = [](double hz, float highCut) {
        DelayCore core = makeClean(10.0f, 10.0f, 50.0f);
        core.setParameterValueDirect(kDelayHighCut, highCut);
        core.prepare(kFs, 480);
        std::vector<float> l(480), r(480);
        float peak = 0.0f;
        for (int b = 0; b < 200; ++b) {
            for (int i = 0; i < 480; ++i) l[(size_t)i] = r[(size_t)i] = 0.1f * (float)std::sin(2.0 * M_PI * hz * (double)(b * 480 + i) / kFs);
            core.process(l.data(), r.data(), 480);
            if (b >= 150) for (int i = 0; i < 480; ++i) peak = std::max(peak, std::abs(l[(size_t)i]));
        }
        return loudio::gainToDb(peak);
    };
    float open = steadyWetDb(10000.0, 20000.0f), cut = steadyWetDb(10000.0, 2000.0f);
    assert(open - cut > 3.0f);                            /* the loop darkens */
    assert(std::abs(steadyWetDb(200.0, 2000.0f) - steadyWetDb(200.0, 20000.0f)) < 0.2f); /* lows untouched */

    /* Mix 0: bit-exact dry, whatever the loop does. */
    DelayCore dry = makeClean(5.0f, 7.0f, 90.0f, 50.0f);
    dry.setParameterValueDirect(kDelayMix, 0.0f);
    dry.prepare(kFs, 512);
    std::vector<float> inL(2048), inR(2048), l, r;
    unsigned seed = 3;
    for (int i = 0; i < 2048; ++i) { seed = seed * 1664525u + 1013904223u; inL[(size_t)i] = ((float)(seed >> 8) / 16777216.0f) * 2.0f - 1.0f; inR[(size_t)i] = -0.5f * inL[(size_t)i]; }
    l = inL; r = inR;
    dry.process(l.data(), r.data(), 2048);
    for (int i = 0; i < 2048; ++i) { assert(l[(size_t)i] == inL[(size_t)i]); assert(r[(size_t)i] == inR[(size_t)i]); }

    /* Feedback 110% is bounded by the soft clip: a long run stays finite and under 2.0. */
    DelayCore hot = makeClean(5.0f, 5.0f, 110.0f);
    hot.prepare(kFs, 512);
    l.assign(512, 0.0f); r.assign(512, 0.0f); l[0] = r[0] = 1.0f;
    float peak = 0.0f;
    for (int b = 0; b < 400; ++b) {
        hot.process(l.data(), r.data(), 512);
        for (int i = 0; i < 512; ++i) { assert(std::isfinite(l[(size_t)i])); peak = std::max(peak, std::abs(l[(size_t)i])); }
        std::fill(l.begin(), l.end(), 0.0f); std::fill(r.begin(), r.end(), 0.0f);
    }
    assert(peak < 2.0f && peak > 0.5f);
    std::cout << "  The loop filters darken repeats and leave lows alone; mix 0 is bit-exact; 110% feedback stays bounded. Pass." << std::endl;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--regen-golden") {
        std::cout << "Regenerating golden impulse responses..." << std::endl;
        loudio::testing::regenerateGoldenImpulseResponses<DelayCore>(LOUDIO_DELAY_GOLDEN_DIR, kGoldenVariants, kGoldenVariantCount, kGoldenIrLength, kGoldenSampleRate);
        return 0;
    }

    std::cout << "===============================" << std::endl;
    std::cout << "Starting Loudio Delay DSP Tests" << std::endl;
    std::cout << "===============================" << std::endl;

    using namespace loudio::testing;
    testParamTableIndexIntegrity<DelayCore>();
    testDefaultsMatchTable<DelayCore>();
    testDirectSetAppliesImmediately<DelayCore>(kDelayFeedback, 55.0f);
    testQueuedSetAppliesAfterProcess<DelayCore>(kDelayFeedback, 55.0f);
    testBatchIsAllOrNothing<DelayCore>(kDelayFeedback, 55.0f);
    testStateRoundTrip<DelayCore>(kStateMagic);
    testStateRejectsCorruptBlobs<DelayCore>();
    testProcessPerformsNoHeapAllocations<DelayCore>(44100.0, 512, 16, [](DelayCore& core) {
        loudio::ParamUpdate u[3] = { { kDelayTimeL, 800.0f }, { kDelaySync, 1.0f }, { kDelayHighCut, 3000.0f } };
        core.setParameterValues(u, 3);
        core.setParameterValue(kDelayModDepth, 4.0f);
    });
    testDelayEchoesAreSampleExact();
    testDelayPingPong();
    testDelayTempoSync();
    testDelayTimeGlideIsClickFree();
    testDelayFeedbackFiltersAndMix();
    testImpulseResponseMatchesGolden<DelayCore>(LOUDIO_DELAY_GOLDEN_DIR, kGoldenVariants, kGoldenVariantCount, kGoldenIrLength, kGoldenSampleRate);

    std::cout << "All Delay DSP tests passed successfully!" << std::endl;
    return 0;
}
