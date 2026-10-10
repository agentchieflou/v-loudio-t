/*
 * AutoTune DSP tests (#146): the platform's generic core checks (minus
 * the golden impulse response, which a time-variant pitch corrector is
 * deliberately exempt from) plus the AutoTune's own: YIN within 1 cent
 * on synthetic tones from 110 to 880 Hz, a sharp A lands on A4 exactly
 * with retune speed 0, scale quantisation, retune speed and humanise,
 * latency == the analysis window with the dry path delayed to match,
 * unvoiced material passing through untouched, and formant shifts
 * staying bounded.
 *
 *   dsp_tests                 run everything
 */
#include "AutoTuneCore.h"
#include "loudio/testing/CoreTests.h"
#include "loudio/dsp/Decibels.h"
#include "loudio/dsp/PitchDetectorYin.h"

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

namespace {

const char kStateMagic[4] = { 'A', 'T', 'U', 'N' };
const double kFs = 48000.0;

float cents(double measuredHz, double referenceHz) { return (float)(1200.0 * std::log2(measuredHz / referenceHz)); }

/* A voice-like tone: fundamental plus 2nd and 3rd harmonics. */
float voiceTone(double hz, long long n, double amp = 0.3) {
    double t = (double)n / kFs;
    return (float)(amp * (std::sin(2.0 * M_PI * hz * t) + 0.5 * std::sin(2.0 * M_PI * 2.0 * hz * t + 1.0) + 0.25 * std::sin(2.0 * M_PI * 3.0 * hz * t + 2.0)));
}

/* Runs `seconds` of a tone through a prepared core (480-sample blocks)
 * and returns the output's f0 measured by a fresh YIN on the last
 * window, plus the output's peak level over the last 100 ms. */
double measureOutputHz(AutoTuneCore& core, double hz, double seconds, float* outPeak = nullptr) {
    const int block = 480, n = (int)(kFs * seconds);
    std::vector<float> l((size_t)block), r((size_t)block), out;
    out.reserve((size_t)n);
    for (int start = 0; start + block <= n; start += block) {
        for (int i = 0; i < block; ++i) l[(size_t)i] = r[(size_t)i] = voiceTone(hz, start + i);
        core.process(l.data(), r.data(), block);
        out.insert(out.end(), l.begin(), l.end());
    }
    loudio::PitchDetectorYin yin;
    yin.prepare(kFs, AutoTuneCore::kYinWindow, 60.0f, 1500.0f);
    std::vector<float> tail(out.end() - yin.requiredSamples(), out.end());
    float f0, ap;
    bool voiced = yin.estimate(tail.data(), f0, ap);
    assert(voiced);
    if (outPeak) { *outPeak = 0.0f; for (size_t i = out.size() - 4800; i < out.size(); ++i) *outPeak = std::max(*outPeak, std::abs(out[i])); }
    return f0;
}

AutoTuneCore makeCore(int scale, int key, float retuneMs, float humanise = 0.0f) {
    AutoTuneCore core;
    core.setParameterValueDirect(kAutoTuneScale, (float)scale);
    core.setParameterValueDirect(kAutoTuneKey, (float)key);
    core.setParameterValueDirect(kAutoTuneRetuneSpeed, retuneMs);
    core.setParameterValueDirect(kAutoTuneHumanise, humanise);
    core.prepare(kFs, 480);
    return core;
}

void testAutoTuneDetectorWithinOneCent() {
    std::cout << "Running testAutoTuneDetectorWithinOneCent..." << std::endl;
    loudio::PitchDetectorYin yin;
    yin.prepare(kFs, AutoTuneCore::kYinWindow, 60.0f, 1500.0f);
    std::vector<float> x((size_t)yin.requiredSamples());
    for (double hz : { 110.0, 123.47, 164.81, 220.0, 329.63, 440.0, 587.33, 659.26, 783.99, 880.0 }) {
        for (size_t n = 0; n < x.size(); ++n) x[n] = voiceTone(hz, (long long)n);
        float f0, ap;
        assert(yin.estimate(x.data(), f0, ap));
        assert(std::abs(cents(f0, hz)) < 1.0f);
    }
    /* And through the core's own analysis. */
    AutoTuneCore core = makeCore(0, 0, 0.0f);
    measureOutputHz(core, 196.0, 0.3);
    assert(core.detectedVoiced());
    assert(std::abs(cents(core.detectedHz(), 196.0)) < 1.0f);
    /* Noise is not voiced. */
    std::vector<float> noise((size_t)yin.requiredSamples());
    unsigned seed = 3;
    for (auto& v : noise) { seed = seed * 1664525u + 1013904223u; v = ((float)(seed >> 8) / 16777216.0f) * 2.0f - 1.0f; }
    float f0, ap;
    assert(!yin.estimate(noise.data(), f0, ap));
    std::cout << "  YIN within 1 cent from 110 to 880 Hz on harmonic tones; noise reads unvoiced. Pass." << std::endl;
}

void testAutoTuneLandsOnA4() {
    std::cout << "Running testAutoTuneLandsOnA4..." << std::endl;
    /* The #146 acceptance point: a sharp A (445 Hz, +19.6 cents), retune
     * speed 0, chromatic -> the output is A4 = 440 Hz within 1 cent, at
     * the input's level within 1 dB. 440 in stays 440. */
    float peak = 0.0f;
    AutoTuneCore sharp = makeCore(0, 0, 0.0f);
    double out = measureOutputHz(sharp, 445.0, 1.0, &peak);
    assert(std::abs(cents(out, 440.0)) < 1.0f);
    assert(sharp.targetMidi() == 69);
    assert(std::abs(sharp.correctionCents() + 19.56f) < 1.0f);
    float inPeak = 0.0f;
    for (int n = 0; n < 4800; ++n) inPeak = std::max(inPeak, std::abs(voiceTone(445.0, n)));
    assert(std::abs(loudio::gainToDb(peak) - loudio::gainToDb(inPeak)) < 1.0f);

    AutoTuneCore flat = makeCore(0, 0, 0.0f);
    out = measureOutputHz(flat, 436.0, 1.0);
    assert(std::abs(cents(out, 440.0)) < 1.0f);
    AutoTuneCore exact = makeCore(0, 0, 0.0f);
    out = measureOutputHz(exact, 440.0, 1.0);
    assert(std::abs(cents(out, 440.0)) < 1.0f);
    std::cout << "  445 / 436 / 440 Hz in -> 440 Hz out within 1 cent, level within 1 dB. Pass." << std::endl;
}

void testAutoTuneScaleQuantisation() {
    std::cout << "Running testAutoTuneScaleQuantisation..." << std::endl;
    /* C major: 470 Hz (just above A#4) -> B4 493.88; 455 Hz -> A4. A minor
     * pentatonic (A C D E G): 500 Hz (near B4) -> C5 523.25. Key E major:
     * 265 Hz (a sharp C4, not in E major) -> C#4 277.18 (the nearest
     * member; an exact C4 sits halfway between B and C# and goes down). */
    assert(AutoTuneCore::quantise(70.4f, 0, kAutoTuneScaleMasks[1]) == 71);
    assert(AutoTuneCore::quantise(69.6f, 0, kAutoTuneScaleMasks[1]) == 69);
    assert(AutoTuneCore::quantise(60.3f, 4, kAutoTuneScaleMasks[1]) == 61);
    assert(AutoTuneCore::quantise(60.0f, 4, kAutoTuneScaleMasks[1]) == 59);
    AutoTuneCore cMajor = makeCore(1, 0, 0.0f);
    assert(std::abs(cents(measureOutputHz(cMajor, 470.0, 1.0), 493.883)) < 1.0f);
    AutoTuneCore aPent = makeCore(6, 9, 0.0f);
    assert(std::abs(cents(measureOutputHz(aPent, 500.0, 1.0), 523.251)) < 1.0f);
    AutoTuneCore eMajor = makeCore(1, 4, 0.0f);
    assert(std::abs(cents(measureOutputHz(eMajor, 265.0, 1.0), 277.183)) < 1.0f);
    /* Reference 432: 432 Hz is A4 and stays; 440 is corrected down to 432. */
    AutoTuneCore ref = makeCore(0, 0, 0.0f);
    ref.setParameterValueDirect(kAutoTuneReference, 432.0f);
    ref.prepare(kFs, 480);
    assert(std::abs(cents(measureOutputHz(ref, 440.0, 1.0), 432.0)) < 1.0f);
    std::cout << "  Key/scale quantisation picks the nearest scale note; the reference pitch applies. Pass." << std::endl;
}

void testAutoTuneRetuneSpeedAndHumanise() {
    std::cout << "Running testAutoTuneRetuneSpeedAndHumanise..." << std::endl;
    /* Retune 200 ms on a sharp A: the applied correction approaches the
     * full -19.6 cents with the one-pole's time constant: about 63% after
     * 200 ms, 95% after 600 ms, monotonically (read off correctionCents
     * at block ends). The first frame of a NEW note snaps, so start from
     * silence -> the tone is a new note: its first voiced frame lands at
     * the target at once; to see the glide, start a second, different
     * note while voiced. */
    AutoTuneCore core = makeCore(0, 0, 200.0f);
    const int block = 480;
    std::vector<float> l((size_t)block), r((size_t)block);
    for (int b = 0; b < 100; ++b) { /* 1 s of an in-tune 392 Hz G4 */
        for (int i = 0; i < block; ++i) l[(size_t)i] = r[(size_t)i] = voiceTone(392.0, b * block + i);
        core.process(l.data(), r.data(), block);
    }
    assert(std::abs(core.correctionCents()) < 1.0f);
    std::vector<float> trace; /* then jump to 445 Hz (A4 + 19.6 c) while voiced: the correction glides */
    for (int b = 0; b < 100; ++b) {
        for (int i = 0; i < block; ++i) l[(size_t)i] = r[(size_t)i] = voiceTone(445.0, 48000 + b * block + i);
        core.process(l.data(), r.data(), block);
        trace.push_back(core.correctionCents());
    }
    /* The detector sees the new pitch after about one window (~40 ms). */
    assert(trace[99] < -18.5f);                     /* settled (95%+) by 1 s */
    assert(trace[25] > -16.0f && trace[25] < -8.0f); /* partway at 250 ms (~210 ms of glide) */
    for (size_t i = 10; i < trace.size(); ++i) assert(trace[i] <= trace[i - 1] + 0.3f); /* monotonic */

    /* Humanise 100%: no correction, 445 stays 445. 50%: half of it. */
    AutoTuneCore human = makeCore(0, 0, 0.0f, 100.0f);
    assert(std::abs(cents(measureOutputHz(human, 445.0, 1.0), 445.0)) < 1.0f);
    AutoTuneCore half = makeCore(0, 0, 0.0f, 50.0f);
    assert(std::abs(cents(measureOutputHz(half, 445.0, 1.0), 442.49)) < 1.0f);
    std::cout << "  Retune speed glides the correction with its time constant; humanise keeps part of the deviation. Pass." << std::endl;
}

void testAutoTuneLatencyAndPassthrough() {
    std::cout << "Running testAutoTuneLatencyAndPassthrough..." << std::endl;
    AutoTuneCore core = makeCore(0, 0, 0.0f);
    const int L = core.latencySamples();
    assert(L == core.analysisWindowSamples());
    assert(L == 2048 + 800); /* 2048 + fs / 60 Hz at 48 kHz */

    /* Mix 0: the dry path delayed by exactly L, bit for bit. */
    AutoTuneCore dry = makeCore(0, 0, 0.0f);
    dry.setParameterValueDirect(kAutoTuneMix, 0.0f);
    dry.prepare(kFs, 512);
    std::vector<float> inL(8192), inR(8192), l, r;
    unsigned seed = 11;
    for (int i = 0; i < 8192; ++i) { seed = seed * 1664525u + 1013904223u; inL[(size_t)i] = ((float)(seed >> 8) / 16777216.0f) * 2.0f - 1.0f; inR[(size_t)i] = -0.5f * inL[(size_t)i]; }
    l = inL; r = inR;
    dry.process(l.data(), r.data(), 8192);
    for (int i = L; i < 8192; ++i) { assert(l[(size_t)i] == inL[(size_t)(i - L)]); assert(r[(size_t)i] == inR[(size_t)(i - L)]); }

    /* Mix 100 on noise (unvoiced): the shifter is a passthrough, so the
     * output is the input delayed by L within float precision. */
    AutoTuneCore wet = makeCore(0, 0, 0.0f);
    l = inL; r = inR;
    wet.process(l.data(), r.data(), 8192);
    float worst = 0.0f;
    for (int i = L + 1024; i < 8192; ++i) worst = std::max(worst, std::abs(l[(size_t)i] - inL[(size_t)(i - L)]));
    assert(worst < 1e-4f);
    std::cout << "  Latency 2848 = the analysis window; mix 0 is the bit-exact delayed dry; unvoiced material passes through within 1e-4. Pass." << std::endl;
}

void testAutoTuneFormantShiftBounded() {
    std::cout << "Running testAutoTuneFormantShiftBounded..." << std::endl;
    for (float semis : { -12.0f, -5.0f, 7.0f, 12.0f }) {
        AutoTuneCore core = makeCore(0, 0, 0.0f);
        core.setParameterValueDirect(kAutoTuneFormant, semis);
        core.prepare(kFs, 480);
        float peak = 0.0f;
        std::vector<float> l(480), r(480);
        for (int b = 0; b < 100; ++b) {
            for (int i = 0; i < 480; ++i) l[(size_t)i] = r[(size_t)i] = voiceTone(220.0, b * 480 + i);
            core.process(l.data(), r.data(), 480);
            for (int i = 0; i < 480; ++i) { assert(std::isfinite(l[(size_t)i])); peak = std::max(peak, std::abs(l[(size_t)i])); }
        }
        assert(peak < 1.5f && peak > 0.05f);
    }
    std::cout << "  Formant shifts from -12 to +12 semitones stay finite and at a sane level. Pass." << std::endl;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--regen-golden") {
        std::cout << "The AutoTune is time-variant and has no golden impulse response (#146)." << std::endl;
        return 0;
    }

    std::cout << "==================================" << std::endl;
    std::cout << "Starting Loudio AutoTune DSP Tests" << std::endl;
    std::cout << "==================================" << std::endl;

    using namespace loudio::testing;
    testParamTableIndexIntegrity<AutoTuneCore>();
    testDefaultsMatchTable<AutoTuneCore>();
    testDirectSetAppliesImmediately<AutoTuneCore>(kAutoTuneRetuneSpeed, 100.0f);
    testQueuedSetAppliesAfterProcess<AutoTuneCore>(kAutoTuneRetuneSpeed, 100.0f);
    testBatchIsAllOrNothing<AutoTuneCore>(kAutoTuneRetuneSpeed, 100.0f);
    testStateRoundTrip<AutoTuneCore>(kStateMagic);
    testStateRejectsCorruptBlobs<AutoTuneCore>();
    testProcessPerformsNoHeapAllocations<AutoTuneCore>(44100.0, 512, 16, [](AutoTuneCore& core) {
        loudio::ParamUpdate u[3] = { { kAutoTuneScale, 1.0f }, { kAutoTuneInputType, 2.0f }, { kAutoTuneFormant, 3.0f } };
        core.setParameterValues(u, 3);
        core.setParameterValue(kAutoTuneRetuneSpeed, 50.0f);
    });
    testAutoTuneDetectorWithinOneCent();
    testAutoTuneLandsOnA4();
    testAutoTuneScaleQuantisation();
    testAutoTuneRetuneSpeedAndHumanise();
    testAutoTuneLatencyAndPassthrough();
    testAutoTuneFormantShiftBounded();

    std::cout << "All AutoTune DSP tests passed successfully!" << std::endl;
    return 0;
}
