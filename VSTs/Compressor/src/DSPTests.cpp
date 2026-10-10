/*
 * Compressor DSP tests (#137): the platform's generic core checks
 * (DSP/include/loudio/testing/CoreTests.h) plus the Compressor's own:
 * static ratio (-20 dBFS sine, threshold -30, 4:1 -> -27.5 dBFS within
 * 0.05 dB), the soft knee, attack/release reaching 63% in one time
 * constant, latency fixed at the maximum lookahead (and the audio really delayed by
 * that), mix 0 nulling against the input, RMS detection, auto makeup, the
 * sidechain high-pass, and golden impulse responses.
 *
 *   dsp_tests                 run everything
 *   dsp_tests --regen-golden  rewrite test/golden/ after an INTENTIONAL DSP change
 */
#include "CompressorCore.h"
#include "loudio/testing/CoreTests.h"
#include "loudio/dsp/Decibels.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

LOUDIO_DEFINE_ALLOCATION_HOOK();

#ifndef LOUDIO_COMPRESSOR_GOLDEN_DIR
#define LOUDIO_COMPRESSOR_GOLDEN_DIR "test/golden"
#endif

namespace {

const char kStateMagic[4] = { 'C', 'O', 'M', 'P' };
const int kGoldenIrLength = 8192;
const double kGoldenSampleRate = 44100.0;
const double kFs = 48000.0;

void goldenHeavy(CompressorCore& c) {
    c.setParameterValueDirect(kCompressorThreshold, -40.0f);
    c.setParameterValueDirect(kCompressorRatio, 10.0f);
    c.setParameterValueDirect(kCompressorKnee, 0.0f);
    c.setParameterValueDirect(kCompressorAttack, 1.0f);
    c.setParameterValueDirect(kCompressorRelease, 50.0f);
    c.setParameterValueDirect(kCompressorLookahead, 2.0f);
    c.setParameterValueDirect(kCompressorMakeup, 6.0f);
}
void goldenRms(CompressorCore& c) {
    c.setParameterValueDirect(kCompressorDetector, (float)kCompressorDetectorRms);
    c.setParameterValueDirect(kCompressorThreshold, -30.0f);
    c.setParameterValueDirect(kCompressorAutoMakeup, 1.0f);
}

const loudio::testing::GoldenVariant<CompressorCore> kGoldenVariants[] = {
    { "default", nullptr },
    { "heavy", goldenHeavy },
    { "rms", goldenRms },
};
const int kGoldenVariantCount = (int)(sizeof(kGoldenVariants) / sizeof(kGoldenVariants[0]));

/* A core set up for exact static measurements: hard knee, fast attack. */
CompressorCore makeStatic(float thresholdDb, float ratio, float kneeDb = 0.0f) {
    CompressorCore core;
    core.setParameterValueDirect(kCompressorThreshold, thresholdDb);
    core.setParameterValueDirect(kCompressorRatio, ratio);
    core.setParameterValueDirect(kCompressorKnee, kneeDb);
    core.setParameterValueDirect(kCompressorAttack, 1.0f);
    core.setParameterValueDirect(kCompressorRelease, 100.0f);
    /* Maximum lookahead: the sidechain delay is then 0, so gain traces
     * read in input time (the audio comes out 10 ms later). */
    core.setParameterValueDirect(kCompressorLookahead, kCompressorMaxLookaheadMs);
    core.prepare(kFs, 512);
    return core;
}

/* Runs a sine of the given peak dBFS through the core for one second and
 * returns the peak output amplitude in dBFS over the last 100 ms. */
double measureSineOutDb(CompressorCore& core, double hz, double inDbfs, double seconds = 1.0) {
    const int n = (int)(kFs * seconds), block = 480;
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

void testCompressorStaticRatio() {
    std::cout << "Running testCompressorStaticRatio..." << std::endl;
    /* The #137 acceptance point: -20 dBFS sine, threshold -30, 4:1 ->
     * 10 dB over at 4:1 is 2.5 dB out over, i.e. -27.5 dBFS. */
    CompressorCore core = makeStatic(-30.0f, 4.0f);
    double out = measureSineOutDb(core, 1000.0, -20.0);
    assert(std::abs(out + 27.5) < 0.05);

    /* Below the threshold nothing happens; a 20:1 ratio is nearly a limiter. */
    CompressorCore quiet = makeStatic(-30.0f, 4.0f);
    assert(std::abs(measureSineOutDb(quiet, 1000.0, -40.0) + 40.0) < 0.05);
    CompressorCore hard = makeStatic(-30.0f, 20.0f);
    assert(std::abs(measureSineOutDb(hard, 1000.0, -10.0) + 29.0) < 0.05);

    std::cout << "  -20 dBFS at threshold -30, 4:1 -> -27.5 dBFS (within 0.05 dB); below-threshold untouched; 20:1 near-limits. Pass." << std::endl;
}

void testCompressorSoftKnee() {
    std::cout << "Running testCompressorSoftKnee..." << std::endl;
    /* 12 dB knee centred on -20: at the threshold the quadratic knee
     * gives (1/R - 1) (W/2)^2 / (2W) = -0.75 * 36 / 24 = -1.125 dB; at
     * the knee's edges it meets the hard curve. */
    CompressorCore at = makeStatic(-20.0f, 4.0f, 12.0f);
    assert(std::abs(measureSineOutDb(at, 1000.0, -20.0) + 21.125) < 0.05);
    CompressorCore below = makeStatic(-20.0f, 4.0f, 12.0f);
    assert(std::abs(measureSineOutDb(below, 1000.0, -26.0) + 26.0) < 0.05);
    CompressorCore above = makeStatic(-20.0f, 4.0f, 12.0f);
    assert(std::abs(measureSineOutDb(above, 1000.0, -14.0) + 18.5) < 0.05);
    std::cout << "  Quadratic knee: -1.125 dB at the threshold, hard curve at the knee edges. Pass." << std::endl;
}

/* DC blocks make the detector's target gain reduction exact, so the
 * follower's time constants can be read directly off meterGainReductionDb
 * (the block's maximum; with 1 ms blocks and a monotonic trajectory that
 * is the value at the block end). */
float runDc(CompressorCore& core, float level, int blocks, std::vector<float>& trace) {
    std::vector<float> l(48), r(48);
    for (int b = 0; b < blocks; ++b) {
        std::fill(l.begin(), l.end(), level);
        std::fill(r.begin(), r.end(), level);
        core.process(l.data(), r.data(), 48);
        trace.push_back(core.meterGainReductionDb());
    }
    return trace.back();
}

void testCompressorAttackRelease() {
    std::cout << "Running testCompressorAttackRelease..." << std::endl;
    /* 0 dBFS DC at threshold -20, 20:1: 19 dB of reduction in the end.
     * Attack 10 ms: after 10 blocks of 1 ms the reduction is 63.2% there. */
    CompressorCore core = makeStatic(-20.0f, 20.0f);
    core.setParameterValueDirect(kCompressorAttack, 10.0f);
    core.setParameterValueDirect(kCompressorRelease, 100.0f);
    core.prepare(kFs, 48);
    std::vector<float> trace;
    runDc(core, 1.0f, 10, trace);
    const float full = 19.0f;
    assert(std::abs(trace[9] / full - 0.632f) < 0.02f);
    runDc(core, 1.0f, 200, trace);
    assert(std::abs(trace.back() - full) < 0.01f);

    /* Release 100 ms: drop to the threshold (target 0 dB) and the
     * reduction is at 36.8% after 100 ms (attack set short so only the
     * release shapes the decay). */
    core.setParameterValueDirect(kCompressorAttack, 0.1f);
    std::vector<float> down;
    runDc(core, 0.1f, 100, down);
    assert(std::abs(down[99] / full - 0.368f) < 0.02f);
    /* ...and the whole trajectory is monotonic: no bounce. */
    for (size_t i = 1; i < down.size(); ++i) assert(down[i] <= down[i - 1] + 1e-4f);

    std::cout << "  Gain reduction reaches 63% in one attack time and decays to 37% in one release time. Pass." << std::endl;
}

void testCompressorLatencyIsFixedMaximum() {
    std::cout << "Running testCompressorLatencyIsFixedMaximum..." << std::endl;
    /* lpi.latency.v1: fixed for an activation. The core always reports
     * (and delays by) the maximum lookahead, 10 ms = 480 samples at
     * 48 kHz, whatever the parameter is set to. */
    CompressorCore core;
    core.setParameterValueDirect(kCompressorLookahead, 5.0f);
    core.setParameterValueDirect(kCompressorRatio, 1.0f); /* 1:1 never reduces: a pure delay */
    core.prepare(kFs, 512);
    assert(core.latencySamples() == 480);

    std::vector<float> l(512, 0.0f), r(512, 0.0f);
    l[0] = 1.0f; r[0] = -1.0f;
    core.process(l.data(), r.data(), 512);
    for (int i = 0; i < 512; ++i) {
        float expectL = i == 480 ? 1.0f : 0.0f;
        assert(std::abs(l[(size_t)i] - expectL) < 1e-6f);
        assert(std::abs(r[(size_t)i] + expectL) < 1e-6f);
    }

    /* The parameter changes the sidechain's head start, not the latency. */
    assert(core.setParameterValue(kCompressorLookahead, 0.0f));
    core.process(l.data(), r.data(), 512);
    assert(core.latencySamples() == 480);
    core.setParameterValueDirect(kCompressorLookahead, kCompressorMaxLookaheadMs);
    core.process(l.data(), r.data(), 512);
    assert(core.latencySamples() == 480);

    /* Lookahead earns its name: with 5 ms lookahead and a 5 ms attack, a
     * burst that starts at sample 480 is already being turned down when
     * it arrives at the output (sample 480 + latency); with 0 ms the gain
     * change and the audio arrive together and the first sample passes. */
    auto firstBurstPeak = [](float lookaheadMs) {
        CompressorCore c = makeStatic(-30.0f, 20.0f);
        c.setParameterValueDirect(kCompressorAttack, 5.0f);
        c.setParameterValueDirect(kCompressorLookahead, lookaheadMs);
        c.prepare(kFs, 2048);
        std::vector<float> a(2048, 0.0f), b(2048, 0.0f);
        for (int i = 480; i < 2048; ++i) a[(size_t)i] = b[(size_t)i] = 1.0f;
        c.process(a.data(), b.data(), 2048);
        int at = 480 + c.latencySamples();
        return a[(size_t)at];
    };
    assert(firstBurstPeak(0.0f) > 0.95f);  /* the first sample of the burst passes almost untouched */
    assert(firstBurstPeak(5.0f) < 0.35f);  /* with lookahead it arrives already attenuated */

    std::cout << "  latencySamples() is the fixed 10 ms maximum, the audio is delayed by exactly that, and the lookahead parameter still catches transients early. Pass." << std::endl;
}

void testCompressorMixZeroNulls() {
    std::cout << "Running testCompressorMixZeroNulls..." << std::endl;
    CompressorCore core = makeStatic(-40.0f, 20.0f);
    core.setParameterValueDirect(kCompressorMix, 0.0f);
    core.setParameterValueDirect(kCompressorMakeup, 12.0f);
    core.prepare(kFs, 512);
    std::vector<float> l(4096), r(4096), inL(4096), inR(4096);
    unsigned seed = 7;
    for (int i = 0; i < 4096; ++i) {
        seed = seed * 1664525u + 1013904223u;
        inL[(size_t)i] = l[(size_t)i] = ((float)(seed >> 8) / 16777216.0f) * 2.0f - 1.0f;
        inR[(size_t)i] = r[(size_t)i] = -inL[(size_t)i] * 0.7f;
    }
    core.process(l.data(), r.data(), 4096);
    assert(core.meterGainReductionDb() > 20.0f); /* it was compressing hard... */
    const int latency = core.latencySamples();
    for (int i = latency; i < 4096; ++i) {
        assert(l[(size_t)i] == inL[(size_t)(i - latency)]); /* ...and none of it reached the (delayed) output */
        assert(r[(size_t)i] == inR[(size_t)(i - latency)]);
    }

    /* Mix 50%: halfway between dry and wet, in linear amplitude. */
    CompressorCore half = makeStatic(-30.0f, 4.0f);
    half.setParameterValueDirect(kCompressorMix, 50.0f);
    half.prepare(kFs, 512);
    double out = std::pow(10.0, measureSineOutDb(half, 1000.0, -20.0) / 20.0);
    double dry = 0.1, wet = std::pow(10.0, -27.5 / 20.0);
    assert(std::abs(out - 0.5 * (dry + wet)) < 0.0005);

    std::cout << "  Mix 0 is bit-exact dry; mix 50 is the linear midpoint. Pass." << std::endl;
}

void testCompressorRmsDetector() {
    std::cout << "Running testCompressorRmsDetector..." << std::endl;
    /* A -20 dBFS sine has -23 dBFS RMS: 7 dB over -30 at 4:1 is 5.25 dB
     * of reduction, so the (still -20 dBFS peak) sine comes out at -25.25. */
    CompressorCore core = makeStatic(-30.0f, 4.0f);
    core.setParameterValueDirect(kCompressorDetector, (float)kCompressorDetectorRms);
    core.prepare(kFs, 512);
    assert(std::abs(measureSineOutDb(core, 1000.0, -20.0) + 25.25) < 0.1);
    std::cout << "  RMS mode reads a sine 3 dB lower than peak mode. Pass." << std::endl;
}

void testCompressorAutoMakeup() {
    std::cout << "Running testCompressorAutoMakeup..." << std::endl;
    /* Threshold -20, 4:1: a 0 dBFS signal would get 15 dB of reduction,
     * so auto makeup adds 15 dB and a quiet (-60 dBFS) sine, which sees
     * no reduction, comes out at -45. The Makeup knob is ignored. */
    CompressorCore core = makeStatic(-20.0f, 4.0f);
    core.setParameterValueDirect(kCompressorAutoMakeup, 1.0f);
    core.setParameterValueDirect(kCompressorMakeup, 3.0f);
    core.prepare(kFs, 512);
    assert(std::abs(core.effectiveMakeupDb() - 15.0f) < 1e-4f);
    assert(std::abs(measureSineOutDb(core, 1000.0, -60.0) + 45.0) < 0.05);

    CompressorCore manual = makeStatic(-20.0f, 4.0f);
    manual.setParameterValueDirect(kCompressorMakeup, 3.0f);
    manual.prepare(kFs, 512);
    assert(std::abs(measureSineOutDb(manual, 1000.0, -60.0) + 57.0) < 0.05);

    std::cout << "  Auto makeup compensates the reduction at 0 dBFS; manual makeup applies otherwise. Pass." << std::endl;
}

void testCompressorSidechainHpf() {
    std::cout << "Running testCompressorSidechainHpf..." << std::endl;
    /* A 0 dBFS 30 Hz sine: 15 dB of reduction normally; with the
     * sidechain high-passed at 300 Hz the detector sees it 40 dB down
     * (2nd order, ~3.3 octaves) and leaves it alone. */
    CompressorCore plain = makeStatic(-20.0f, 4.0f);
    assert(measureSineOutDb(plain, 30.0, 0.0) < -14.0);
    CompressorCore hpf = makeStatic(-20.0f, 4.0f);
    hpf.setParameterValueDirect(kCompressorScHpf, 300.0f);
    hpf.prepare(kFs, 512);
    assert(measureSineOutDb(hpf, 30.0, 0.0) > -0.1);
    std::cout << "  Sidechain HPF keeps bass from driving the detector. Pass." << std::endl;
}

void testCompressorMeters() {
    std::cout << "Running testCompressorMeters..." << std::endl;
    CompressorCore core = makeStatic(-30.0f, 4.0f);
    measureSineOutDb(core, 1000.0, -20.0);
    assert(std::abs(core.meterInputDb() + 20.0f) < 0.05f);
    assert(std::abs(core.meterOutputDb() + 27.5f) < 0.05f);
    assert(std::abs(core.meterGainReductionDb() - 7.5f) < 0.05f);
    std::cout << "  Per-block in/out/GR readings agree with the signal. Pass." << std::endl;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--regen-golden") {
        std::cout << "Regenerating golden impulse responses..." << std::endl;
        loudio::testing::regenerateGoldenImpulseResponses<CompressorCore>(LOUDIO_COMPRESSOR_GOLDEN_DIR, kGoldenVariants, kGoldenVariantCount, kGoldenIrLength, kGoldenSampleRate);
        return 0;
    }

    std::cout << "====================================" << std::endl;
    std::cout << "Starting Loudio Compressor DSP Tests" << std::endl;
    std::cout << "====================================" << std::endl;

    using namespace loudio::testing;
    testParamTableIndexIntegrity<CompressorCore>();
    testDefaultsMatchTable<CompressorCore>();
    testDirectSetAppliesImmediately<CompressorCore>(kCompressorThreshold, -12.0f);
    testQueuedSetAppliesAfterProcess<CompressorCore>(kCompressorThreshold, -12.0f);
    testBatchIsAllOrNothing<CompressorCore>(kCompressorThreshold, -12.0f);
    testStateRoundTrip<CompressorCore>(kStateMagic);
    testStateRejectsCorruptBlobs<CompressorCore>();
    testProcessPerformsNoHeapAllocations<CompressorCore>(44100.0, 512, 16, [](CompressorCore& core) {
        loudio::ParamUpdate u[3] = { { kCompressorThreshold, -35.0f }, { kCompressorLookahead, 3.0f }, { kCompressorDetector, 1.0f } };
        core.setParameterValues(u, 3);
        core.setParameterValue(kCompressorAutoMakeup, 1.0f);
    });
    testCompressorStaticRatio();
    testCompressorSoftKnee();
    testCompressorAttackRelease();
    testCompressorLatencyIsFixedMaximum();
    testCompressorMixZeroNulls();
    testCompressorRmsDetector();
    testCompressorAutoMakeup();
    testCompressorSidechainHpf();
    testCompressorMeters();
    testImpulseResponseMatchesGolden<CompressorCore>(LOUDIO_COMPRESSOR_GOLDEN_DIR, kGoldenVariants, kGoldenVariantCount, kGoldenIrLength, kGoldenSampleRate);

    std::cout << "All Compressor DSP tests passed successfully!" << std::endl;
    return 0;
}
