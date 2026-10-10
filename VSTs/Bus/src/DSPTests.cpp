/*
 * Bus DSP tests (#141): the platform's generic core checks plus the
 * Bus's own: neutral settings are a bit-exact passthrough, the pan law
 * is constant power with centre -3 dB per side relative to a hard pan,
 * width 0 gives identical channels (and 200 doubles the side), inverting
 * both phases nulls against the input when summed, swap and mono are
 * exact, gain applies after its ramp, the DC blocker removes DC and
 * passes audio, and golden impulse responses.
 *
 *   dsp_tests                 run everything
 *   dsp_tests --regen-golden  rewrite test/golden/ after an INTENTIONAL DSP change
 */
#include "BusCore.h"
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

#ifndef LOUDIO_BUS_GOLDEN_DIR
#define LOUDIO_BUS_GOLDEN_DIR "test/golden"
#endif

namespace {

const char kStateMagic[4] = { 'B', 'U', 'S', '_' };
const int kGoldenIrLength = 8192;
const double kGoldenSampleRate = 44100.0;
const double kFs = 48000.0;

void goldenShaped(BusCore& c) {
    c.setParameterValueDirect(kBusGain, -3.0f);
    c.setParameterValueDirect(kBusPan, 30.0f);
    c.setParameterValueDirect(kBusWidth, 150.0f);
    c.setParameterValueDirect(kBusDcBlock, 1.0f);
    c.setParameterValueDirect(kBusPhaseR, 1.0f);
}

const loudio::testing::GoldenVariant<BusCore> kGoldenVariants[] = {
    { "default", nullptr },
    { "shaped", goldenShaped },
};
const int kGoldenVariantCount = (int)(sizeof(kGoldenVariants) / sizeof(kGoldenVariants[0]));

/* Deterministic stereo noise. */
void fillNoise(std::vector<float>& l, std::vector<float>& r, unsigned seed) {
    for (size_t i = 0; i < l.size(); ++i) {
        seed = seed * 1664525u + 1013904223u;
        l[i] = ((float)(seed >> 8) / 16777216.0f) * 2.0f - 1.0f;
        seed = seed * 1664525u + 1013904223u;
        r[i] = ((float)(seed >> 8) / 16777216.0f) * 2.0f - 1.0f;
    }
}

/* Runs 2048 samples through a prepared core twice (the first pass covers
 * any 20 ms ramp) and returns the second pass's output. */
void runSettled(BusCore& core, const std::vector<float>& inL, const std::vector<float>& inR, std::vector<float>& outL, std::vector<float>& outR) {
    outL = inL; outR = inR;
    core.process(outL.data(), outR.data(), (int)outL.size());
    outL = inL; outR = inR;
    core.process(outL.data(), outR.data(), (int)outL.size());
}

void testBusNeutralIsBitExact() {
    std::cout << "Running testBusNeutralIsBitExact..." << std::endl;
    BusCore core;
    core.prepare(kFs, 2048);
    std::vector<float> inL(2048), inR(2048), l, r;
    fillNoise(inL, inR, 5);
    runSettled(core, inL, inR, l, r);
    for (size_t i = 0; i < l.size(); ++i) { assert(l[i] == inL[i]); assert(r[i] == inR[i]); }
    std::cout << "  Every parameter at its default: output == input, bit for bit. Pass." << std::endl;
}

void testBusPanLaw() {
    std::cout << "Running testBusPanLaw..." << std::endl;
    /* Constant power: gL^2 + gR^2 == 2 everywhere; centre is unity, a
     * hard pan is +3.01 dB on the kept side (so centre is -3 dB per side
     * relative to hard), the other side silent. */
    for (float pan = -1.0f; pan <= 1.0f; pan += 0.125f) {
        float gl, gr;
        BusCore::panGains(pan, gl, gr);
        assert(std::abs(gl * gl + gr * gr - 2.0f) < 1e-4f);
    }
    float gl, gr;
    BusCore::panGains(0.0f, gl, gr);
    assert(gl == 1.0f && gr == 1.0f);
    BusCore::panGains(-1.0f, gl, gr);
    assert(std::abs(loudio::gainToDb(gl) - 3.01f) < 0.01f && gr < 1e-6f);

    /* And the core applies it: hard left on a sine, left is +3 dB, right silent. */
    BusCore core;
    core.setParameterValueDirect(kBusPan, -100.0f);
    core.prepare(kFs, 2048);
    std::vector<float> inL(2048), inR(2048), l, r;
    for (int i = 0; i < 2048; ++i) inL[(size_t)i] = inR[(size_t)i] = (float)std::sin(2.0 * M_PI * 1000.0 * (double)i / kFs);
    runSettled(core, inL, inR, l, r);
    float peakL = 0.0f, peakR = 0.0f;
    for (size_t i = 0; i < l.size(); ++i) { peakL = std::max(peakL, std::abs(l[i])); peakR = std::max(peakR, std::abs(r[i])); }
    assert(std::abs(loudio::gainToDb(peakL) - 3.01f) < 0.02f);
    assert(peakR < 1e-6f);
    std::cout << "  Constant-power pan: centre unity, hard pan +3 dB on the kept side (centre = -3 dB per side), other side silent. Pass." << std::endl;
}

void testBusWidth() {
    std::cout << "Running testBusWidth..." << std::endl;
    BusCore mono;
    mono.setParameterValueDirect(kBusWidth, 0.0f);
    mono.prepare(kFs, 2048);
    std::vector<float> inL(2048), inR(2048), l, r;
    fillNoise(inL, inR, 9);
    runSettled(mono, inL, inR, l, r);
    for (size_t i = 0; i < l.size(); ++i) {
        assert(l[i] == r[i]); /* width 0: identical channels */
        assert(std::abs(l[i] - 0.5f * (inL[i] + inR[i])) < 1e-6f);
    }
    /* Width 200 on a pure side signal (R = -L) doubles it. */
    BusCore wide;
    wide.setParameterValueDirect(kBusWidth, 200.0f);
    wide.prepare(kFs, 2048);
    for (size_t i = 0; i < inL.size(); ++i) inR[i] = -inL[i];
    runSettled(wide, inL, inR, l, r);
    for (size_t i = 0; i < l.size(); ++i) { assert(std::abs(l[i] - 2.0f * inL[i]) < 1e-6f); assert(std::abs(r[i] + 2.0f * inL[i]) < 1e-6f); }
    std::cout << "  Width 0 gives identical channels (the mid); width 200 doubles the side. Pass." << std::endl;
}

void testBusPhaseSwapMono() {
    std::cout << "Running testBusPhaseSwapMono..." << std::endl;
    std::vector<float> inL(2048), inR(2048), l, r;
    fillNoise(inL, inR, 21);
    BusCore inv;
    inv.setParameterValueDirect(kBusPhaseL, 1.0f);
    inv.setParameterValueDirect(kBusPhaseR, 1.0f);
    inv.prepare(kFs, 2048);
    runSettled(inv, inL, inR, l, r);
    for (size_t i = 0; i < l.size(); ++i) { assert(l[i] + inL[i] == 0.0f); assert(r[i] + inR[i] == 0.0f); } /* sums to exactly zero */

    BusCore swap;
    swap.setParameterValueDirect(kBusSwap, 1.0f);
    swap.prepare(kFs, 2048);
    runSettled(swap, inL, inR, l, r);
    for (size_t i = 0; i < l.size(); ++i) { assert(l[i] == inR[i]); assert(r[i] == inL[i]); }

    BusCore mono;
    mono.setParameterValueDirect(kBusMono, 1.0f);
    mono.prepare(kFs, 2048);
    runSettled(mono, inL, inR, l, r);
    for (size_t i = 0; i < l.size(); ++i) { assert(l[i] == r[i]); assert(l[i] == 0.5f * (inL[i] + inR[i])); }
    std::cout << "  Both phases inverted null against the input; swap and mono are exact. Pass." << std::endl;
}

void testBusGainAndDcBlock() {
    std::cout << "Running testBusGainAndDcBlock..." << std::endl;
    BusCore core;
    core.setParameterValueDirect(kBusGain, -6.0f);
    core.prepare(kFs, 2048);
    std::vector<float> inL(2048, 1.0f), inR(2048, -0.5f), l, r;
    runSettled(core, inL, inR, l, r);
    assert(std::abs(l[2047] - loudio::dbToGain(-6.0f)) < 1e-6f);
    assert(std::abs(r[2047] + 0.5f * loudio::dbToGain(-6.0f)) < 1e-6f);
    /* A queued gain change ramps: no sample step larger than 1/960 of the
     * full change (20 ms at 48 kHz) and it lands exactly. */
    assert(core.setParameterValue(kBusGain, 0.0f));
    l = inL; r = inR;
    core.process(l.data(), r.data(), 2048);
    float maxStep = 0.0f;
    for (size_t i = 1; i < l.size(); ++i) maxStep = std::max(maxStep, std::abs(l[i] - l[i - 1]));
    assert(maxStep < (1.0f - loudio::dbToGain(-6.0f)) / 900.0f);
    assert(l[2047] == 1.0f);

    /* DC blocker: 0.5 of DC decays to under 1e-3 within a second; a 1 kHz
     * sine passes within 0.05 dB. */
    BusCore dc;
    dc.setParameterValueDirect(kBusDcBlock, 1.0f);
    dc.prepare(kFs, 480);
    std::vector<float> a(480), b(480);
    for (int blk = 0; blk < 100; ++blk) {
        for (int i = 0; i < 480; ++i) { a[(size_t)i] = 0.5f + 0.1f * (float)std::sin(2.0 * M_PI * 1000.0 * (double)(blk * 480 + i) / kFs); b[(size_t)i] = 0.5f; }
        dc.process(a.data(), b.data(), 480);
    }
    double mean = 0.0; float peak = 0.0f;
    for (int i = 0; i < 480; ++i) { mean += a[(size_t)i]; peak = std::max(peak, std::abs(a[(size_t)i])); }
    assert(std::abs(mean / 480.0) < 1e-3);
    assert(std::abs(b[479]) < 1e-3f);
    assert(std::abs(loudio::gainToDb(peak) - loudio::gainToDb(0.1f)) < 0.05f);
    std::cout << "  Gain applies after a click-free 20 ms ramp; the DC blocker removes DC and passes 1 kHz within 0.05 dB. Pass." << std::endl;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--regen-golden") {
        std::cout << "Regenerating golden impulse responses..." << std::endl;
        loudio::testing::regenerateGoldenImpulseResponses<BusCore>(LOUDIO_BUS_GOLDEN_DIR, kGoldenVariants, kGoldenVariantCount, kGoldenIrLength, kGoldenSampleRate);
        return 0;
    }

    std::cout << "=============================" << std::endl;
    std::cout << "Starting Loudio Bus DSP Tests" << std::endl;
    std::cout << "=============================" << std::endl;

    using namespace loudio::testing;
    testParamTableIndexIntegrity<BusCore>();
    testDefaultsMatchTable<BusCore>();
    testDirectSetAppliesImmediately<BusCore>(kBusGain, -3.0f);
    testQueuedSetAppliesAfterProcess<BusCore>(kBusGain, -3.0f);
    testBatchIsAllOrNothing<BusCore>(kBusGain, -3.0f);
    testStateRoundTrip<BusCore>(kStateMagic);
    testStateRejectsCorruptBlobs<BusCore>();
    testProcessPerformsNoHeapAllocations<BusCore>(44100.0, 512, 16, [](BusCore& core) {
        loudio::ParamUpdate u[3] = { { kBusGain, -3.0f }, { kBusPan, 40.0f }, { kBusWidth, 120.0f } };
        core.setParameterValues(u, 3);
        core.setParameterValue(kBusDcBlock, 1.0f);
    });
    testBusNeutralIsBitExact();
    testBusPanLaw();
    testBusWidth();
    testBusPhaseSwapMono();
    testBusGainAndDcBlock();
    testImpulseResponseMatchesGolden<BusCore>(LOUDIO_BUS_GOLDEN_DIR, kGoldenVariants, kGoldenVariantCount, kGoldenIrLength, kGoldenSampleRate);

    std::cout << "All Bus DSP tests passed successfully!" << std::endl;
    return 0;
}
