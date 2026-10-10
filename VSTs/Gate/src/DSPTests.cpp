/*
 * Gate DSP tests (#138): the platform's generic core checks
 * (DSP/include/loudio/testing/CoreTests.h) plus the Gate's own: a burst
 * opens within the attack time and closes after hold + release, a level
 * toggling +-1 dB around the threshold never chatters with hysteresis,
 * range -20 dB leaves exactly -20 dB when closed, latency == lookahead,
 * the sidechain high-pass, and golden impulse responses.
 *
 *   dsp_tests                 run everything
 *   dsp_tests --regen-golden  rewrite test/golden/ after an INTENTIONAL DSP change
 */
#include "GateCore.h"
#include "loudio/testing/CoreTests.h"
#include "loudio/dsp/Decibels.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

LOUDIO_DEFINE_ALLOCATION_HOOK();

#ifndef LOUDIO_GATE_GOLDEN_DIR
#define LOUDIO_GATE_GOLDEN_DIR "test/golden"
#endif

namespace {

const char kStateMagic[4] = { 'G', 'A', 'T', 'E' };
const int kGoldenIrLength = 8192;
const double kGoldenSampleRate = 44100.0;
const double kFs = 48000.0;

void goldenExpander(GateCore& c) {
    c.setParameterValueDirect(kGateThreshold, -30.0f);
    c.setParameterValueDirect(kGateRange, -20.0f);
    c.setParameterValueDirect(kGateHold, 10.0f);
    c.setParameterValueDirect(kGateRelease, 30.0f);
    c.setParameterValueDirect(kGateLookahead, 1.0f);
}

const loudio::testing::GoldenVariant<GateCore> kGoldenVariants[] = {
    { "default", nullptr },
    { "expander", goldenExpander },
};
const int kGoldenVariantCount = (int)(sizeof(kGoldenVariants) / sizeof(kGoldenVariants[0]));

/* Runs 1 ms blocks of a 1 kHz sine at the given peak dBFS (or silence for
 * <= -200) and appends the gain at each block end to `trace`. */
void runBlocks(GateCore& core, double inDbfs, int blocks, std::vector<float>& trace, int& sampleIndex) {
    std::vector<float> l(48), r(48);
    const double amp = inDbfs <= -200.0 ? 0.0 : std::pow(10.0, inDbfs / 20.0);
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < 48; ++i) {
            l[(size_t)i] = r[(size_t)i] = (float)(amp * std::sin(2.0 * M_PI * 1000.0 * (double)sampleIndex / kFs));
            ++sampleIndex;
        }
        core.process(l.data(), r.data(), 48);
        trace.push_back(core.currentGain());
    }
}

GateCore makeGate(float thresholdDb, float hysteresisDb, float attackMs, float holdMs, float releaseMs) {
    GateCore core;
    core.setParameterValueDirect(kGateThreshold, thresholdDb);
    core.setParameterValueDirect(kGateHysteresis, hysteresisDb);
    core.setParameterValueDirect(kGateAttack, attackMs);
    core.setParameterValueDirect(kGateHold, holdMs);
    core.setParameterValueDirect(kGateRelease, releaseMs);
    core.prepare(kFs, 48);
    return core;
}

void testGateBurstOpensAndCloses() {
    std::cout << "Running testGateBurstOpensAndCloses..." << std::endl;
    /* Threshold -40, attack 10 ms, hold 50 ms, release 100 ms. A -20 dBFS
     * burst for 200 ms, then silence. */
    GateCore core = makeGate(-40.0f, 6.0f, 10.0f, 50.0f, 100.0f);
    std::vector<float> trace;
    int n = 0;
    assert(core.currentGain() == 0.0f); /* starts closed */
    runBlocks(core, -20.0, 200, trace, n);
    /* Opens within the attack time: 63% of the way at 10 ms. (The sine's
     * first peak is 0.25 ms in, so the gain at the 10 ms block end sits
     * just under 0.632.) */
    assert(trace[9] > 0.55f && trace[9] < 0.66f);
    assert(trace[199] > 0.999f); /* fully open long before the burst ends */

    /* Silence: the detector takes ~5 ms * ln(10) = 11.5 ms to decay the
     * 20 dB from -20 to the -46 dB close threshold... */
    std::vector<float> tail;
    runBlocks(core, -300.0, 400, tail, n);
    /* ...then the hold keeps it fully open: at 40 ms the gain is still 1. */
    assert(tail[39] > 0.999f);
    /* After hold + release (plus the detector's lag) the gain is at 37%:
     * find where it crosses e^-1 and compare with 11.5 + 50 + 100 ms. */
    int cross = -1;
    for (size_t i = 0; i < tail.size(); ++i) if (tail[i] < 0.3679f) { cross = (int)i + 1; break; }
    assert(cross > 0);
    assert(std::abs(cross - (13 + 50 + 100)) <= 6); /* 1 ms blocks; 13 ms is the detector's 20 dB decay plus the close's own sample */
    assert(tail[399] < 0.05f && tail[399] < tail[300]); /* ~e^-3.4 by 400 ms, still closing toward silence */

    std::cout << "  Opens to 63% within the attack time, holds, then closes to 37% one release after the hold. Pass." << std::endl;
}

void testGateHysteresisPreventsChatter() {
    std::cout << "Running testGateHysteresisPreventsChatter..." << std::endl;
    /* A level toggling between threshold + 1 dB and threshold - 1 dB
     * every 20 ms (hold 0 so only the hysteresis can keep it open): with
     * 6 dB of hysteresis the gate opens once and never closes; with 0 dB
     * it chatters on every toggle. */
    auto countTransitions = [](float hysteresisDb) {
        GateCore core = makeGate(-40.0f, hysteresisDb, 0.1f, 0.0f, 1.0f);
        int n = 0, transitions = 0;
        bool wasOpen = core.meterOpen();
        for (int toggle = 0; toggle < 20; ++toggle) {
            std::vector<float> trace;
            runBlocks(core, toggle % 2 == 0 ? -39.0 : -41.0, 20, trace, n);
            if (core.meterOpen() != wasOpen) { ++transitions; wasOpen = core.meterOpen(); }
        }
        return transitions;
    };
    assert(countTransitions(6.0f) == 1);   /* open, and stays open */
    assert(countTransitions(0.0f) >= 15);  /* chatter without hysteresis */
    std::cout << "  +-1 dB around the threshold: one transition with 6 dB hysteresis, chatter with none. Pass." << std::endl;
}

void testGateRangeFloor() {
    std::cout << "Running testGateRangeFloor..." << std::endl;
    /* A steady -60 dBFS sine below a -40 threshold with range -20 dB
     * comes out exactly 20 dB down once the release has settled. */
    GateCore core = makeGate(-40.0f, 6.0f, 1.0f, 0.0f, 10.0f);
    core.setParameterValueDirect(kGateRange, -20.0f);
    core.prepare(kFs, 48);
    std::vector<float> l(48), r(48);
    double amp = std::pow(10.0, -60.0 / 20.0);
    float worst = 0.0f;
    int n = 0;
    for (int b = 0; b < 500; ++b) {
        std::vector<float> in(48);
        for (int i = 0; i < 48; ++i) {
            in[(size_t)i] = (float)(amp * std::sin(2.0 * M_PI * 1000.0 * (double)n / kFs));
            l[(size_t)i] = r[(size_t)i] = in[(size_t)i];
            ++n;
        }
        core.process(l.data(), r.data(), 48);
        if (b >= 400) for (int i = 0; i < 48; ++i) worst = std::max(worst, std::abs(l[(size_t)i] - 0.1f * in[(size_t)i]));
    }
    assert(worst < 1e-7f); /* 0.1 x input, to float precision */
    assert(std::abs(core.meterGainReductionDb() - 20.0f) < 0.01f);

    /* Full range: silence. */
    GateCore shut = makeGate(-40.0f, 6.0f, 1.0f, 0.0f, 10.0f);
    for (int b = 0; b < 500; ++b) {
        for (int i = 0; i < 48; ++i) l[(size_t)i] = r[(size_t)i] = (float)(amp * std::sin(2.0 * M_PI * 1000.0 * (double)(b * 48 + i) / kFs));
        shut.process(l.data(), r.data(), 48);
    }
    for (int i = 0; i < 48; ++i) assert(l[(size_t)i] == 0.0f);
    std::cout << "  Range -20 dB leaves exactly -20 dB when closed; -80 dB is silence. Pass." << std::endl;
}

void testGateLatencyIsLookahead() {
    std::cout << "Running testGateLatencyIsLookahead..." << std::endl;
    GateCore core;
    core.setParameterValueDirect(kGateLookahead, 5.0f);
    core.setParameterValueDirect(kGateThreshold, -80.0f); /* always open */
    core.setParameterValueDirect(kGateAttack, 0.01f);
    core.prepare(kFs, 512);
    assert(core.latencySamples() == 240);

    /* Let it open on a steady signal, then send an impulse. */
    std::vector<float> l(512, 0.01f), r(512, 0.01f);
    for (int b = 0; b < 10; ++b) { std::fill(l.begin(), l.end(), 0.01f); std::fill(r.begin(), r.end(), 0.01f); core.process(l.data(), r.data(), 512); }
    std::fill(l.begin(), l.end(), 0.01f);
    std::fill(r.begin(), r.end(), 0.01f);
    l[0] = 1.0f; r[0] = -1.0f;
    core.process(l.data(), r.data(), 512);
    for (int i = 0; i < 512; ++i) {
        float expectL = i == 240 ? 1.0f : 0.01f;
        assert(std::abs(l[(size_t)i] - expectL) < 1e-5f);
    }
    assert(std::abs(r[240] + 1.0f) < 1e-5f);

    assert(core.setParameterValue(kGateLookahead, 0.0f));
    core.process(l.data(), r.data(), 512);
    assert(core.latencySamples() == 0);

    /* Lookahead earns its name: with 5 ms lookahead and a 5 ms attack, a
     * burst's first sample is already half open at the output. */
    auto firstSample = [](float lookaheadMs) {
        GateCore c = makeGate(-40.0f, 6.0f, 5.0f, 50.0f, 100.0f);
        c.setParameterValueDirect(kGateLookahead, lookaheadMs);
        c.prepare(kFs, 2048);
        std::vector<float> a(2048, 0.0f), b(2048, 0.0f);
        for (int i = 480; i < 2048; ++i) a[(size_t)i] = b[(size_t)i] = 0.5f;
        c.process(a.data(), b.data(), 2048);
        return a[(size_t)(480 + c.latencySamples())];
    };
    assert(firstSample(0.0f) < 0.01f);  /* the first sample is still shut */
    assert(firstSample(5.0f) > 0.3f);   /* with lookahead it arrives mostly open */

    std::cout << "  latencySamples() == lookahead samples, audio delayed by exactly that, transients arrive open. Pass." << std::endl;
}

void testGateSidechainHpf() {
    std::cout << "Running testGateSidechainHpf..." << std::endl;
    /* A -20 dBFS 30 Hz rumble opens a -40 threshold gate; with the
     * sidechain high-passed at 300 Hz the detector sees it ~40 dB down
     * and the gate stays shut. */
    auto openOn30Hz = [](float hpfHz) {
        GateCore core = makeGate(-40.0f, 6.0f, 1.0f, 0.0f, 10.0f);
        core.setParameterValueDirect(kGateScHpf, hpfHz);
        core.prepare(kFs, 480);
        std::vector<float> l(480), r(480);
        for (int b = 0; b < 100; ++b) {
            for (int i = 0; i < 480; ++i) l[(size_t)i] = r[(size_t)i] = 0.1f * (float)std::sin(2.0 * M_PI * 30.0 * (double)(b * 480 + i) / kFs);
            core.process(l.data(), r.data(), 480);
        }
        return core.meterOpen();
    };
    assert(openOn30Hz(0.0f));
    assert(!openOn30Hz(300.0f));
    std::cout << "  Sidechain HPF keeps rumble from opening the gate. Pass." << std::endl;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--regen-golden") {
        std::cout << "Regenerating golden impulse responses..." << std::endl;
        loudio::testing::regenerateGoldenImpulseResponses<GateCore>(LOUDIO_GATE_GOLDEN_DIR, kGoldenVariants, kGoldenVariantCount, kGoldenIrLength, kGoldenSampleRate);
        return 0;
    }

    std::cout << "==============================" << std::endl;
    std::cout << "Starting Loudio Gate DSP Tests" << std::endl;
    std::cout << "==============================" << std::endl;

    using namespace loudio::testing;
    testParamTableIndexIntegrity<GateCore>();
    testDefaultsMatchTable<GateCore>();
    testDirectSetAppliesImmediately<GateCore>(kGateThreshold, -30.0f);
    testQueuedSetAppliesAfterProcess<GateCore>(kGateThreshold, -30.0f);
    testBatchIsAllOrNothing<GateCore>(kGateThreshold, -30.0f);
    testStateRoundTrip<GateCore>(kStateMagic);
    testStateRejectsCorruptBlobs<GateCore>();
    testProcessPerformsNoHeapAllocations<GateCore>(44100.0, 512, 16, [](GateCore& core) {
        loudio::ParamUpdate u[3] = { { kGateThreshold, -35.0f }, { kGateLookahead, 3.0f }, { kGateRange, -20.0f } };
        core.setParameterValues(u, 3);
        core.setParameterValue(kGateScHpf, 100.0f);
    });
    testGateBurstOpensAndCloses();
    testGateHysteresisPreventsChatter();
    testGateRangeFloor();
    testGateLatencyIsLookahead();
    testGateSidechainHpf();
    testImpulseResponseMatchesGolden<GateCore>(LOUDIO_GATE_GOLDEN_DIR, kGoldenVariants, kGoldenVariantCount, kGoldenIrLength, kGoldenSampleRate);

    std::cout << "All Gate DSP tests passed successfully!" << std::endl;
    return 0;
}
