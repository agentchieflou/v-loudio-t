/*
 * EQ-8 DSP tests: the platform's generic core checks plus the EQ-8's own
 * (#136): magnitude at 1 kHz for a +6 dB bell within 0.1 dB of the
 * coefficient formula, flat response nulling against the input, the
 * UI-side response helper agreeing with the audio path, bypass, and a
 * golden impulse response per band type.
 *
 *   dsp_tests                 run everything
 *   dsp_tests --regen-golden  rewrite test/golden/ after an INTENTIONAL DSP change
 */
#include "EQ8Core.h"
#include "EQ8Response.h"
#include "loudio/testing/CoreTests.h"
#include "loudio/dsp/Decibels.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

LOUDIO_DEFINE_ALLOCATION_HOOK();

#ifndef LOUDIO_EQ8_GOLDEN_DIR
#define LOUDIO_EQ8_GOLDEN_DIR "test/golden"
#endif

namespace {

const char kStateMagic[4] = { 'E', 'Q', '8', '_' };
const int kGoldenIrLength = 8192;
const double kGoldenSampleRate = 44100.0;

void setBand(EQ8Core& core, int band, int type, float hz, float gain, float q) {
    core.setParameterValueDirect(eq8BandParam(band, kEQ8BandType), (float)type);
    core.setParameterValueDirect(eq8BandParam(band, kEQ8BandFreq), hz);
    core.setParameterValueDirect(eq8BandParam(band, kEQ8BandGain), gain);
    core.setParameterValueDirect(eq8BandParam(band, kEQ8BandQ), q);
}

/* One golden IR per band type on band 1 (everything else flat). */
void goldenBell(EQ8Core& c)      { setBand(c, 0, kEQ8Bell, 1000.0f, 6.0f, 1.0f); }
void goldenLowShelf(EQ8Core& c)  { setBand(c, 0, kEQ8LowShelf, 200.0f, 9.0f, 0.707f); }
void goldenHighShelf(EQ8Core& c) { setBand(c, 0, kEQ8HighShelf, 5000.0f, -6.0f, 0.707f); }
void goldenHighPass(EQ8Core& c)  { setBand(c, 0, kEQ8HighPass, 100.0f, 0.0f, 0.707f); }
void goldenLowPass(EQ8Core& c)   { setBand(c, 0, kEQ8LowPass, 8000.0f, 0.0f, 0.707f); }
void goldenNotch(EQ8Core& c)     { setBand(c, 0, kEQ8Notch, 1000.0f, 0.0f, 4.0f); }
void goldenAll(EQ8Core& c) {
    setBand(c, 0, kEQ8HighPass, 40.0f, 0.0f, 0.707f);
    setBand(c, 1, kEQ8LowShelf, 120.0f, 3.0f, 0.707f);
    setBand(c, 2, kEQ8Bell, 400.0f, -4.0f, 2.0f);
    setBand(c, 3, kEQ8Bell, 1200.0f, 5.0f, 1.0f);
    setBand(c, 4, kEQ8Notch, 3000.0f, 0.0f, 6.0f);
    setBand(c, 5, kEQ8Bell, 6000.0f, 2.0f, 0.8f);
    setBand(c, 6, kEQ8HighShelf, 10000.0f, -3.0f, 0.707f);
    setBand(c, 7, kEQ8LowPass, 16000.0f, 0.0f, 0.707f);
    c.setParameterValueDirect(kEQ8OutputGain, 1.5f);
}

const loudio::testing::GoldenVariant<EQ8Core> kGoldenVariants[] = {
    { "default", nullptr },
    { "bell", goldenBell },
    { "lowshelf", goldenLowShelf },
    { "highshelf", goldenHighShelf },
    { "highpass", goldenHighPass },
    { "lowpass", goldenLowPass },
    { "notch", goldenNotch },
    { "all", goldenAll },
};
const int kGoldenVariantCount = (int)(sizeof(kGoldenVariants) / sizeof(kGoldenVariants[0]));

/* RMS amplitude in dB of a sine through the core, over the second half second. */
double measureCoreDb(EQ8Core& core, double hz, double fs) {
    const int n = (int)fs, block = 512;
    std::vector<float> l((size_t)block), r((size_t)block);
    double sum = 0.0; int count = 0;
    for (int start = 0; start < n; start += block) {
        for (int i = 0; i < block; ++i) l[(size_t)i] = r[(size_t)i] = (float)std::sin(2.0 * M_PI * hz * (double)(start + i) / fs);
        core.process(l.data(), r.data(), block);
        if (start >= n / 2) for (int i = 0; i < block; ++i) { sum += (double)l[(size_t)i] * l[(size_t)i]; ++count; }
    }
    return 20.0 * std::log10(std::sqrt(2.0 * sum / (double)count));
}

void testEQ8DefaultsAreFlat() {
    std::cout << "Running testEQ8DefaultsAreFlat..." << std::endl;
    /* All bands at 0 dB bells plus 0 dB output: the EQ-8 must null against
     * its input within 0.01 dB (the plugin spec); the double biquads do far
     * better than that. */
    EQ8Core core;
    core.prepare(48000.0, 512);
    std::vector<float> l(4096), r(4096), inL(4096), inR(4096);
    unsigned seed = 3;
    for (int i = 0; i < 4096; ++i) {
        seed = seed * 1664525u + 1013904223u;
        inL[(size_t)i] = l[(size_t)i] = ((float)(seed >> 8) / 16777216.0f) * 2.0f - 1.0f;
        inR[(size_t)i] = r[(size_t)i] = -inL[(size_t)i] * 0.5f;
    }
    core.process(l.data(), r.data(), 4096);
    float worst = 0.0f;
    for (int i = 0; i < 4096; ++i) {
        worst = std::max(worst, std::abs(l[(size_t)i] - inL[(size_t)i]));
        worst = std::max(worst, std::abs(r[(size_t)i] - inR[(size_t)i]));
    }
    assert(worst < 1e-6f);
    std::cout << "  Default EQ-8 passes noise through within 1e-6 (spec: 0.01 dB). Pass." << std::endl;
}

void testEQ8BellAt1kHzMatchesFormula() {
    std::cout << "Running testEQ8BellAt1kHzMatchesFormula..." << std::endl;
    EQ8Core core;
    core.prepare(48000.0, 512);
    setBand(core, 2, kEQ8Bell, 1000.0f, 6.0f, 1.0f);
    double measured = measureCoreDb(core, 1000.0, 48000.0);
    assert(std::abs(measured - 6.0) < 0.1); /* the #136 acceptance point */

    /* And the UI-side response helper says the same thing as the audio path. */
    float params[kEQ8NumParams];
    kEQ8ParamTable.fillDefaults(params);
    params[eq8BandParam(2, kEQ8BandType)] = (float)kEQ8Bell;
    params[eq8BandParam(2, kEQ8BandFreq)] = 1000.0f;
    params[eq8BandParam(2, kEQ8BandGain)] = 6.0f;
    params[eq8BandParam(2, kEQ8BandQ)] = 1.0f;
    float response[3];
    eq8ComputeResponse(params, 48000.0, 1000.0f, 1000.0f, response, 1);
    assert(std::abs(response[0] - 6.0f) < 0.01f);
    eq8ComputeResponse(params, 48000.0, 20.0f, 20000.0f, response, 3); /* 20, 632, 20000 Hz */
    assert(std::abs(response[0]) < 0.1f && std::abs(response[2]) < 0.1f);
    EQ8Core probe;
    probe.prepare(48000.0, 512);
    setBand(probe, 2, kEQ8Bell, 1000.0f, 6.0f, 1.0f);
    assert(std::abs(measureCoreDb(probe, 632.456f, 48000.0) - response[1]) < 0.1);

    std::cout << "  +6 dB bell at 1 kHz measures +6 dB (within 0.1 dB); eq8ComputeResponse agrees with the audio path. Pass." << std::endl;
}

void testEQ8OutputGainAndBypass() {
    std::cout << "Running testEQ8OutputGainAndBypass..." << std::endl;
    EQ8Core core;
    core.prepare(48000.0, 512);
    setBand(core, 0, kEQ8HighPass, 2000.0f, 0.0f, 0.707f); /* kills 100 Hz */
    core.setParameterValueDirect(kEQ8OutputGain, -6.0f);
    assert(measureCoreDb(core, 100.0, 48000.0) < -40.0);
    assert(std::abs(measureCoreDb(core, 10000.0, 48000.0) + 6.0) < 0.2);
    core.setParameterValueDirect(kEQ8Bypass, 1.0f);
    assert(std::abs(measureCoreDb(core, 100.0, 48000.0)) < 0.01); /* bypass: untouched, no output gain either */

    std::cout << "  Output gain applies after the bands; bypass passes the input untouched. Pass." << std::endl;
}

void testEQ8QueuedBandEditRedesignsWithoutClick() {
    std::cout << "Running testEQ8QueuedBandEditRedesignsWithoutClick..." << std::endl;
    EQ8Core core;
    core.prepare(48000.0, 64);
    setBand(core, 0, kEQ8Bell, 1000.0f, 0.0f, 1.0f);
    std::vector<float> l(64), r(64);
    /* A 100 Hz sine while band 1's gain jumps 0 -> +12 dB through the RT
     * queue (gain automation, the blueprint's de-zippering case): the
     * output stays continuous, every sample step within the signal's own
     * slope. */
    float prev = 0.0f, maxStep = 0.0f, peak = 0.0f;
    const float slope = 2.0f * (float)M_PI * 100.0f / 48000.0f * 1.5f;
    for (int blk = 0; blk < 400; ++blk) {
        if (blk == 200) assert(core.setParameterValue(eq8BandParam(0, kEQ8BandGain), 12.0f));
        for (int i = 0; i < 64; ++i) l[(size_t)i] = r[(size_t)i] = (float)std::sin(2.0 * M_PI * 100.0 * (double)(blk * 64 + i) / 48000.0);
        core.process(l.data(), r.data(), 64);
        for (int i = 0; i < 64; ++i) {
            if (blk > 0 || i > 0) maxStep = std::max(maxStep, std::abs(l[(size_t)i] - prev));
            prev = l[(size_t)i];
        }
    }
    assert(maxStep < slope);
    assert(core.getParameterValue(eq8BandParam(0, kEQ8BandGain)) == 12.0f);

    /* A four-octave frequency jump (60 Hz -> 1 kHz at +12 dB) wobbles
     * briefly as the ramped coefficients pass through intermediate
     * filters; it must stay bounded (never beyond the +12 dB = 4x the
     * bell can give, plus a margin) and settle on the new design. */
    EQ8Core jump;
    jump.prepare(48000.0, 64);
    setBand(jump, 0, kEQ8Bell, 60.0f, 12.0f, 1.0f);
    for (int blk = 0; blk < 400; ++blk) {
        if (blk == 200) {
            loudio::ParamUpdate u[1] = { { eq8BandParam(0, kEQ8BandFreq), 1000.0f } };
            assert(jump.setParameterValues(u, 1));
        }
        for (int i = 0; i < 64; ++i) l[(size_t)i] = r[(size_t)i] = (float)std::sin(2.0 * M_PI * 100.0 * (double)(blk * 64 + i) / 48000.0);
        jump.process(l.data(), r.data(), 64);
        for (int i = 0; i < 64; ++i) peak = std::max(peak, std::abs(l[(size_t)i]));
    }
    assert(peak < 4.5f);
    assert(std::abs(measureCoreDb(jump, 1000.0, 48000.0) - 12.0) < 0.1); /* settled on the new design */

    std::cout << "  Queued gain automation ramps without a click; a 4-octave frequency jump stays bounded and settles. Pass." << std::endl;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--regen-golden") {
        std::cout << "Regenerating golden impulse responses..." << std::endl;
        loudio::testing::regenerateGoldenImpulseResponses<EQ8Core>(LOUDIO_EQ8_GOLDEN_DIR, kGoldenVariants, kGoldenVariantCount, kGoldenIrLength, kGoldenSampleRate);
        return 0;
    }

    std::cout << "==============================" << std::endl;
    std::cout << "Starting Loudio EQ-8 DSP Tests" << std::endl;
    std::cout << "==============================" << std::endl;

    using namespace loudio::testing;
    testParamTableIndexIntegrity<EQ8Core>();
    testDefaultsMatchTable<EQ8Core>();
    testDirectSetAppliesImmediately<EQ8Core>(kEQ8OutputGain, -3.0f);
    testQueuedSetAppliesAfterProcess<EQ8Core>(kEQ8OutputGain, -3.0f);
    testBatchIsAllOrNothing<EQ8Core>(kEQ8OutputGain, -3.0f);
    testStateRoundTrip<EQ8Core>(kStateMagic);
    testStateRejectsCorruptBlobs<EQ8Core>();
    testProcessPerformsNoHeapAllocations<EQ8Core>(44100.0, 512, 16, [](EQ8Core& core) {
        loudio::ParamUpdate u[2] = { { eq8BandParam(3, kEQ8BandGain), 7.0f }, { eq8BandParam(3, kEQ8BandType), (float)kEQ8LowShelf } };
        core.setParameterValues(u, 2);
        core.setParameterValue(kEQ8OutputGain, 2.0f);
    });
    testEQ8DefaultsAreFlat();
    testEQ8BellAt1kHzMatchesFormula();
    testEQ8OutputGainAndBypass();
    testEQ8QueuedBandEditRedesignsWithoutClick();
    testImpulseResponseMatchesGolden<EQ8Core>(LOUDIO_EQ8_GOLDEN_DIR, kGoldenVariants, kGoldenVariantCount, kGoldenIrLength, kGoldenSampleRate);

    std::cout << "All EQ-8 DSP tests passed successfully!" << std::endl;
    return 0;
}
