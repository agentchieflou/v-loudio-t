/*
 * EQ-3 DSP tests (#145): the platform's generic core checks plus the
 * EQ-3's own: all bands at 0 dB sum flat within 0.01 dB from 20 Hz to
 * 20 kHz, killing one band leaves the other two phase-coherent (no notch
 * at their crossover), band gains and kills apply, crossover changes are
 * click-free, and golden impulse responses.
 *
 *   dsp_tests                 run everything
 *   dsp_tests --regen-golden  rewrite test/golden/ after an INTENTIONAL DSP change
 */
#include "EQ3Core.h"
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

#ifndef LOUDIO_EQ3_GOLDEN_DIR
#define LOUDIO_EQ3_GOLDEN_DIR "test/golden"
#endif

namespace {

const char kStateMagic[4] = { 'E', 'Q', '3', '_' };
const int kGoldenIrLength = 8192;
const double kGoldenSampleRate = 44100.0;
const double kFs = 48000.0;

void goldenDj(EQ3Core& c) {
    c.setParameterValueDirect(kEQ3KillLow, 1.0f);
    c.setParameterValueDirect(kEQ3MidGain, -6.0f);
    c.setParameterValueDirect(kEQ3HighGain, 3.0f);
    c.setParameterValueDirect(kEQ3XoverLowMid, 200.0f);
    c.setParameterValueDirect(kEQ3XoverMidHigh, 2000.0f);
}

const loudio::testing::GoldenVariant<EQ3Core> kGoldenVariants[] = {
    { "default", nullptr },
    { "dj", goldenDj },
};
const int kGoldenVariantCount = (int)(sizeof(kGoldenVariants) / sizeof(kGoldenVariants[0]));

/* Magnitude (dB) of a sine through a prepared core: RMS over whole cycles
 * of the second half of a one-second run (the all-pass phase shift makes
 * a sample-wise null meaningless; the magnitude is what must be flat). */
double magnitudeDb(EQ3Core& core, double hz) {
    const int n = 48000, block = 480;
    std::vector<float> l((size_t)block), r((size_t)block);
    double sumIn = 0.0, sumOut = 0.0;
    const int cycles = (int)(hz * 0.5); /* whole cycles in the last half second */
    const int measure = cycles > 0 ? (int)std::round(cycles / hz * kFs) : n / 2;
    for (int start = 0; start < n; start += block) {
        for (int i = 0; i < block; ++i) l[(size_t)i] = r[(size_t)i] = (float)std::sin(2.0 * M_PI * hz * (double)(start + i) / kFs);
        std::vector<float> in = l;
        core.process(l.data(), r.data(), block);
        for (int i = 0; i < block; ++i) {
            int t = start + i;
            if (t >= n - measure) { sumIn += (double)in[(size_t)i] * in[(size_t)i]; sumOut += (double)l[(size_t)i] * l[(size_t)i]; }
        }
    }
    return 10.0 * std::log10(sumOut / sumIn);
}

const double kProbeHz[] = { 20, 30, 50, 80, 120, 200, 300, 400, 600, 900, 1300, 2000, 3000, 4500, 7000, 10000, 14000, 20000 };

void testEQ3FlatSum() {
    std::cout << "Running testEQ3FlatSum..." << std::endl;
    /* The #145 acceptance point: every band at 0 dB nulls against the
     * input within 0.01 dB across 20 Hz - 20 kHz (LR4 sums to an
     * all-pass). Two crossover settings. */
    for (int set = 0; set < 2; ++set) {
        double worst = 0.0;
        for (double hz : kProbeHz) {
            EQ3Core core;
            core.setParameterValueDirect(kEQ3XoverLowMid, set == 0 ? 300.0f : 80.0f);
            core.setParameterValueDirect(kEQ3XoverMidHigh, set == 0 ? 3000.0f : 8000.0f);
            core.prepare(kFs, 480);
            worst = std::max(worst, std::abs(magnitudeDb(core, hz)));
        }
        assert(worst < 0.01);
    }
    std::cout << "  All bands at 0 dB: flat within 0.01 dB from 20 Hz to 20 kHz for two crossover settings. Pass." << std::endl;
}

void testEQ3KillLeavesNoNotch() {
    std::cout << "Running testEQ3KillLeavesNoNotch..." << std::endl;
    /* Kill the low band: at the mid-high crossover (3 kHz) mid and high
     * are in phase, so the sum is still 0 dB there, and from 4 x the
     * low-mid crossover (where the killed band's LR4 slope has it 25 dB
     * down, 0.03 dB of the sum) up to 20 kHz the response stays within
     * 0.1 dB of flat; a phase-incoherent split would leave a notch at
     * 3 kHz. */
    double worst = 0.0;
    for (double hz : { 1200.0, 1500.0, 2500.0, 3000.0, 3500.0, 5000.0, 8000.0, 12000.0, 20000.0 }) {
        EQ3Core core;
        core.setParameterValueDirect(kEQ3KillLow, 1.0f);
        core.prepare(kFs, 480);
        worst = std::max(worst, std::abs(magnitudeDb(core, hz)));
    }
    assert(worst < 0.1);
    /* And the killed band is gone: -40 dB at 50 Hz. */
    EQ3Core core;
    core.setParameterValueDirect(kEQ3KillLow, 1.0f);
    core.prepare(kFs, 480);
    assert(magnitudeDb(core, 50.0) < -40.0);
    /* Kill high: low + mid stay flat through the low-mid crossover
     * (300 Hz) and up to a quarter of the mid-high one. */
    double worst2 = 0.0;
    for (double hz : { 20.0, 50.0, 150.0, 300.0, 450.0, 750.0 }) {
        EQ3Core c;
        c.setParameterValueDirect(kEQ3KillHigh, 1.0f);
        c.prepare(kFs, 480);
        worst2 = std::max(worst2, std::abs(magnitudeDb(c, hz)));
    }
    assert(worst2 < 0.1);
    std::cout << "  Killing a band leaves the other two phase-coherent: no notch at their crossover, within 0.1 dB. Pass." << std::endl;
}

void testEQ3GainsAndKills() {
    std::cout << "Running testEQ3GainsAndKills..." << std::endl;
    EQ3Core core;
    core.setParameterValueDirect(kEQ3LowGain, 12.0f);
    core.setParameterValueDirect(kEQ3HighGain, -12.0f);
    core.prepare(kFs, 480);
    /* Mid-band probes see a little of the boosted neighbour's LR4 slope
     * (the +12 dB low band is still 0.8% at 1 kHz: +0.28 dB), hence the
     * looser tolerances away from the band centres. */
    assert(std::abs(magnitudeDb(core, 50.0) - 12.0) < 0.05);
    assert(std::abs(magnitudeDb(core, 1000.0)) < 0.4);
    assert(std::abs(magnitudeDb(core, 12000.0) + 12.0) < 0.2);
    /* -60 dB on the fader is a kill (gain exactly 0), as is the switch. */
    EQ3Core floor;
    floor.setParameterValueDirect(kEQ3MidGain, kEQ3KillDb);
    floor.prepare(kFs, 480);
    assert(floor.targetBandGain(1) == 0.0f);
    /* What is left at 1 kHz is the neighbours' LR4 slopes (the low band
     * -42 dB at 3.3 x its corner, the high band -38 dB at a third of
     * its): about -35 dB in total. */
    assert(magnitudeDb(floor, 1000.0) < -30.0);
    /* All three killed: exactly silent after the ramps. */
    EQ3Core silent;
    silent.setParameterValueDirect(kEQ3KillLow, 1.0f);
    silent.setParameterValueDirect(kEQ3KillMid, 1.0f);
    silent.setParameterValueDirect(kEQ3KillHigh, 1.0f);
    silent.prepare(kFs, 480);
    std::vector<float> l(480, 0.5f), r(480, -0.5f);
    silent.process(l.data(), r.data(), 480);
    for (int i = 0; i < 480; ++i) { assert(l[(size_t)i] == 0.0f); assert(r[(size_t)i] == 0.0f); }
    std::cout << "  +12 / 0 / -12 dB per band apply; -60 dB and the switch are exact kills. Pass." << std::endl;
}

void testEQ3CrossoverChangeIsClickFree() {
    std::cout << "Running testEQ3CrossoverChangeIsClickFree..." << std::endl;
    /* A 1 kHz sine while the mid-high crossover jumps 3 kHz -> 1.5 kHz
     * through the RT queue: with every band at 0 dB the sum is flat
     * whatever the crossovers are, so the output must stay a clean sine:
     * no sample step beyond the sine's own slope. */
    EQ3Core core;
    core.prepare(kFs, 64);
    std::vector<float> l(64), r(64);
    float prev = 0.0f, maxStep = 0.0f;
    const float slope = 2.0f * (float)M_PI * 1000.0f / 48000.0f * 1.1f;
    for (int blk = 0; blk < 300; ++blk) {
        if (blk == 150) assert(core.setParameterValue(kEQ3XoverMidHigh, 1500.0f));
        for (int i = 0; i < 64; ++i) l[(size_t)i] = r[(size_t)i] = (float)std::sin(2.0 * M_PI * 1000.0 * (double)(blk * 64 + i) / kFs);
        core.process(l.data(), r.data(), 64);
        for (int i = 0; i < 64; ++i) {
            if (blk > 10) maxStep = std::max(maxStep, std::abs(l[(size_t)i] - prev));
            prev = l[(size_t)i];
        }
    }
    assert(maxStep < slope);
    std::cout << "  A crossover jump under a 1 kHz sine produces no step beyond the sine's own slope. Pass." << std::endl;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--regen-golden") {
        std::cout << "Regenerating golden impulse responses..." << std::endl;
        loudio::testing::regenerateGoldenImpulseResponses<EQ3Core>(LOUDIO_EQ3_GOLDEN_DIR, kGoldenVariants, kGoldenVariantCount, kGoldenIrLength, kGoldenSampleRate);
        return 0;
    }

    std::cout << "==============================" << std::endl;
    std::cout << "Starting Loudio EQ-3 DSP Tests" << std::endl;
    std::cout << "==============================" << std::endl;

    using namespace loudio::testing;
    testParamTableIndexIntegrity<EQ3Core>();
    testDefaultsMatchTable<EQ3Core>();
    testDirectSetAppliesImmediately<EQ3Core>(kEQ3LowGain, -3.0f);
    testQueuedSetAppliesAfterProcess<EQ3Core>(kEQ3LowGain, -3.0f);
    testBatchIsAllOrNothing<EQ3Core>(kEQ3LowGain, -3.0f);
    testStateRoundTrip<EQ3Core>(kStateMagic);
    testStateRejectsCorruptBlobs<EQ3Core>();
    testProcessPerformsNoHeapAllocations<EQ3Core>(44100.0, 512, 16, [](EQ3Core& core) {
        loudio::ParamUpdate u[3] = { { kEQ3LowGain, 6.0f }, { kEQ3XoverLowMid, 500.0f }, { kEQ3KillHigh, 1.0f } };
        core.setParameterValues(u, 3);
        core.setParameterValue(kEQ3MidGain, -3.0f);
    });
    testEQ3FlatSum();
    testEQ3KillLeavesNoNotch();
    testEQ3GainsAndKills();
    testEQ3CrossoverChangeIsClickFree();
    testImpulseResponseMatchesGolden<EQ3Core>(LOUDIO_EQ3_GOLDEN_DIR, kGoldenVariants, kGoldenVariantCount, kGoldenIrLength, kGoldenSampleRate);

    std::cout << "All EQ-3 DSP tests passed successfully!" << std::endl;
    return 0;
}
