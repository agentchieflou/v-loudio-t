/*
 * Limiter DSP tests (#139): the platform's generic core checks plus the
 * Limiter's own. The headline is the HARD GUARANTEE: on a worst-case
 * burst (full-scale square stepping from silence, driven +12 dB) no
 * output sample exceeds the ceiling, at every block size, every lookahead
 * and every oversampling mode. Then: true-peak within 0.1 dB of the
 * ceiling on an inter-sample-peak tone with 4x oversampling, latency fixed
 * at the maximum lookahead + 4x upsampler delay (checked against the audio
 * and against the Oversampler itself), release to 63% in one time constant, stereo link,
 * and golden impulse responses.
 *
 *   dsp_tests                 run everything
 *   dsp_tests --regen-golden  rewrite test/golden/ after an INTENTIONAL DSP change
 */
#include "LimiterCore.h"
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

#ifndef LOUDIO_LIMITER_GOLDEN_DIR
#define LOUDIO_LIMITER_GOLDEN_DIR "test/golden"
#endif

namespace {

const char kStateMagic[4] = { 'L', 'I', 'M', 'I' };
const int kGoldenIrLength = 8192;
const double kGoldenSampleRate = 44100.0;
const double kFs = 48000.0;

void goldenTruePeak(LimiterCore& c) {
    c.setParameterValueDirect(kLimiterOversampling, (float)kLimiterOs4x);
    c.setParameterValueDirect(kLimiterCeiling, -6.0f);
    c.setParameterValueDirect(kLimiterInputGain, 6.0f);
}
void goldenUnlinked(LimiterCore& c) {
    c.setParameterValueDirect(kLimiterLink, 0.0f);
    c.setParameterValueDirect(kLimiterLookahead, 0.5f);
    c.setParameterValueDirect(kLimiterRelease, 20.0f);
    c.setParameterValueDirect(kLimiterInputGain, 12.0f);
}

const loudio::testing::GoldenVariant<LimiterCore> kGoldenVariants[] = {
    { "default", nullptr },
    { "truepeak4x", goldenTruePeak },
    { "unlinked", goldenUnlinked },
};
const int kGoldenVariantCount = (int)(sizeof(kGoldenVariants) / sizeof(kGoldenVariants[0]));

LimiterCore makeLimiter(float ceilingDb, float lookaheadMs, int oversampling, float inputGainDb, int maxBlock = 1024) {
    LimiterCore core;
    core.setParameterValueDirect(kLimiterCeiling, ceilingDb);
    core.setParameterValueDirect(kLimiterLookahead, lookaheadMs);
    core.setParameterValueDirect(kLimiterOversampling, (float)oversampling);
    core.setParameterValueDirect(kLimiterInputGain, inputGainDb);
    core.prepare(kFs, maxBlock);
    return core;
}

/* Worst case: silence, then a full-scale square wave (alternating every
 * 37 samples, so edges land on every block-size phase), then silence,
 * then a lone full-scale impulse. 1 second. */
std::vector<float> worstCaseBurst() {
    std::vector<float> x((size_t)kFs, 0.0f);
    for (int n = 4800; n < 28800; ++n) x[(size_t)n] = ((n / 37) % 2 == 0) ? 1.0f : -1.0f;
    x[40000] = 1.0f;
    for (int n = 42000; n < 42010; ++n) x[(size_t)n] = -1.0f;
    return x;
}

void testLimiterHardCeilingGuarantee() {
    std::cout << "Running testLimiterHardCeilingGuarantee..." << std::endl;
    const std::vector<float> burst = worstCaseBurst();
    const int blockSizes[] = { 1, 7, 64, 480, 1024, 4096 /* > maxBlockSize: chunked */ };
    const float lookaheads[] = { 0.0f, 0.5f, 2.0f, 10.0f };
    const int modes[] = { kLimiterOsOff, kLimiterOs2x, kLimiterOs4x };
    const float ceilingDb = -1.0f;
    const float ceiling = loudio::dbToGain(ceilingDb);
    int cases = 0;
    for (int mode : modes) for (float la : lookaheads) for (int block : blockSizes) {
        LimiterCore core = makeLimiter(ceilingDb, la, mode, 12.0f);
        std::vector<float> l = burst, r = burst;
        for (size_t i = 0; i < r.size(); ++i) r[i] *= 0.5f; /* the quiet channel rides the link */
        float peak = 0.0f;
        for (int start = 0; start < (int)l.size(); start += block) {
            int n = std::min(block, (int)l.size() - start);
            core.process(l.data() + (size_t)start, r.data() + (size_t)start, n);
        }
        for (size_t i = 0; i < l.size(); ++i) {
            assert(std::abs(l[i]) <= ceiling); /* THE guarantee: never above the ceiling */
            assert(std::abs(r[i]) <= ceiling);
            peak = std::max(peak, std::abs(l[i]));
        }
        assert(peak > ceiling * 0.98f); /* and it is limiting, not muting */
        ++cases;
    }
    std::cout << "  " << cases << " (mode x lookahead x block size) cases: no output sample above the ceiling, every one within 0.2 dB of it. Pass." << std::endl;
}

/* True peak of a buffer: the largest magnitude of its 4x upsampled
 * version, skipping the first 200 samples (the buffer's abrupt start
 * rings the interpolation filter above the steady-state peak). */
float truePeakDb(const std::vector<float>& x) {
    loudio::Oversampler<4> os;
    os.prepare((int)x.size());
    const float* up = os.upsample(x.data(), (int)x.size());
    float peak = 0.0f;
    for (int i = 800; i < (int)x.size() * 4; ++i) peak = std::max(peak, std::abs(up[i]));
    return loudio::gainToDb(peak);
}

void testLimiterTruePeak() {
    std::cout << "Running testLimiterTruePeak..." << std::endl;
    /* A sine at fs/4 sampled at 45 degrees: every sample is +-0.707
     * (-3.01 dBFS sample peak) but the waveform peaks at 0 dBFS between
     * samples. Ceiling -6 dB. */
    std::vector<float> tone((size_t)kFs);
    for (size_t n = 0; n < tone.size(); ++n) tone[n] = (float)std::sin(2.0 * M_PI * 0.25 * (double)n + M_PI / 4.0);
    assert(std::abs(truePeakDb(tone)) < 0.05f);

    auto run = [&](int mode) {
        LimiterCore core = makeLimiter(-6.0f, 2.0f, mode, 0.0f);
        std::vector<float> l = tone, r = tone;
        for (int start = 0; start < (int)l.size(); start += 480) core.process(l.data() + start, r.data() + start, 480);
        std::vector<float> tail(l.begin() + 24000, l.end());
        return truePeakDb(tail);
    };
    float off = run(kLimiterOsOff), x2 = run(kLimiterOs2x), x4 = run(kLimiterOs4x);
    assert(std::abs(off + 3.0f) < 0.1f);  /* sample-peak limiting only takes it to -3: 3 dB of overs */
    assert(std::abs(x4 + 6.0f) < 0.1f);   /* 4x true-peak: at the ceiling within 0.1 dB */
    assert(x2 < -5.0f);                   /* 2x sees most of it (fs/4 at 2x is exactly the phase that misses) */
    std::cout << "  Inter-sample-peak tone: -3 dB overs without oversampling, true peak at the -6 dB ceiling within 0.1 dB with 4x. Pass." << std::endl;
}

void testLimiterLatencyIsFixed() {
    std::cout << "Running testLimiterLatencyIsFixed..." << std::endl;
    /* The upsampler's own delay, measured: an impulse through upsample()
     * peaks 16 base samples later at 2x, 24 at 4x. */
    {
        loudio::Oversampler<2> os2; os2.prepare(256);
        loudio::Oversampler<4> os4; os4.prepare(256);
        std::vector<float> imp(256, 0.0f); imp[0] = 1.0f;
        const float* u2 = os2.upsample(imp.data(), 256);
        const float* u4 = os4.upsample(imp.data(), 256);
        int p2 = (int)(std::max_element(u2, u2 + 512) - u2), p4 = (int)(std::max_element(u4, u4 + 1024) - u4);
        assert(p2 == 2 * LimiterCore::detectorDelaySamples(kLimiterOs2x));
        assert(p4 == 4 * LimiterCore::detectorDelaySamples(kLimiterOs4x));
    }
    /* Reported latency is fixed (10 ms + 24 samples at 48 kHz) in every
     * mode, and the audio really is delayed by it. */
    const int expected = 480 + 24;
    for (int mode = 0; mode < kLimiterNumOs; ++mode) {
        LimiterCore core = makeLimiter(0.0f, 2.0f, mode, 0.0f);
        assert(core.latencySamples() == expected);
        std::vector<float> l(1024, 0.0f), r(1024, 0.0f);
        l[0] = 0.5f; r[0] = -0.25f;
        core.process(l.data(), r.data(), 1024);
        for (int i = 0; i < 1024; ++i) {
            assert(std::abs(l[(size_t)i] - (i == expected ? 0.5f : 0.0f)) < 1e-6f);
            assert(std::abs(r[(size_t)i] - (i == expected ? -0.25f : 0.0f)) < 1e-6f);
        }
    }
    /* Parameter changes move the detector's head start, not the latency. */
    LimiterCore core = makeLimiter(0.0f, 2.0f, kLimiterOsOff, 0.0f);
    loudio::ParamUpdate u[2] = { { kLimiterLookahead, 10.0f }, { kLimiterOversampling, (float)kLimiterOs4x } };
    assert(core.setParameterValues(u, 2));
    std::vector<float> l(64, 0.0f), r(64, 0.0f);
    core.process(l.data(), r.data(), 64);
    assert(core.latencySamples() == expected);
    std::cout << "  Upsampler delays measured (16 / 24); latencySamples() is the fixed 10 ms + 24 samples in every mode, audio delayed by exactly that. Pass." << std::endl;
}

void testLimiterReleaseTimeConstant() {
    std::cout << "Running testLimiterReleaseTimeConstant..." << std::endl;
    /* Lookahead 0 (window and box of 1) so only the release shapes the
     * recovery: a 6 dB over holds the gain at the ceiling (0.5012); when
     * the signal drops to the ceiling the gain rises as 1 - (1-g) e^(-t/tau):
     * 0.8165 at tau. */
    LimiterCore core = makeLimiter(-6.0f, 0.0f, kLimiterOsOff, 0.0f);
    core.setParameterValueDirect(kLimiterRelease, 100.0f);
    core.prepare(kFs, 48);
    std::vector<float> l(48), r(48);
    auto dc = [&](float level, int blocks, std::vector<float>& trace) {
        for (int b = 0; b < blocks; ++b) {
            std::fill(l.begin(), l.end(), level); std::fill(r.begin(), r.end(), level);
            core.process(l.data(), r.data(), 48);
            trace.push_back(core.currentGainL());
        }
    };
    std::vector<float> on, off;
    dc(1.0f, 50, on);
    const float held = loudio::dbToGain(-6.0f) * 0.99999f;
    assert(std::abs(on.back() - held) < 1e-5f);
    assert(std::abs(l[47] - loudio::dbToGain(-6.0f)) < 1e-4f); /* sits at the ceiling */
    /* With lookahead 0 the detected peaks run through the full 504-sample
     * remainder delay, so the recovery starts 10.5 ms after the drop. */
    dc(loudio::dbToGain(-6.0f), 200, off);
    assert(std::abs(off[110] - (1.0f - (1.0f - held) * std::exp(-1.0f))) < 0.01f);
    for (size_t i = 1; i < off.size(); ++i) assert(off[i] >= off[i - 1]); /* monotonic recovery */
    std::cout << "  Gain recovers 63% of the way in one release time, monotonically. Pass." << std::endl;
}

void testLimiterStereoLink() {
    std::cout << "Running testLimiterStereoLink..." << std::endl;
    /* Left +6 dB over, right 12 dB under the ceiling. Linked: both get the
     * same 6 dB of reduction; unlinked: right is untouched. */
    auto rightGainDb = [](float linkPercent) {
        LimiterCore core = makeLimiter(-6.0f, 1.0f, kLimiterOsOff, 0.0f);
        core.setParameterValueDirect(kLimiterLink, linkPercent);
        core.prepare(kFs, 480);
        std::vector<float> l(480), r(480);
        float peakR = 0.0f;
        for (int b = 0; b < 50; ++b) {
            for (int i = 0; i < 480; ++i) {
                double t = (double)(b * 480 + i) / kFs;
                l[(size_t)i] = (float)std::sin(2.0 * M_PI * 1000.0 * t);
                r[(size_t)i] = loudio::dbToGain(-18.0f) * (float)std::sin(2.0 * M_PI * 1000.0 * t);
            }
            core.process(l.data(), r.data(), 480);
            if (b >= 40) for (int i = 0; i < 480; ++i) peakR = std::max(peakR, std::abs(r[(size_t)i]));
        }
        return loudio::gainToDb(peakR) + 18.0f; /* change relative to the input */
    };
    assert(std::abs(rightGainDb(100.0f) + 6.0f) < 0.1f);
    assert(std::abs(rightGainDb(0.0f)) < 0.05f);
    assert(std::abs(rightGainDb(50.0f) + 2.5f) < 0.1f); /* halfway in linear gain: (1 + 0.5012) / 2 = -2.49 dB */
    std::cout << "  Link 100 shares the loud channel's reduction, link 0 keeps channels independent. Pass." << std::endl;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--regen-golden") {
        std::cout << "Regenerating golden impulse responses..." << std::endl;
        loudio::testing::regenerateGoldenImpulseResponses<LimiterCore>(LOUDIO_LIMITER_GOLDEN_DIR, kGoldenVariants, kGoldenVariantCount, kGoldenIrLength, kGoldenSampleRate);
        return 0;
    }

    std::cout << "=================================" << std::endl;
    std::cout << "Starting Loudio Limiter DSP Tests" << std::endl;
    std::cout << "=================================" << std::endl;

    using namespace loudio::testing;
    testParamTableIndexIntegrity<LimiterCore>();
    testDefaultsMatchTable<LimiterCore>();
    testDirectSetAppliesImmediately<LimiterCore>(kLimiterCeiling, -3.0f);
    testQueuedSetAppliesAfterProcess<LimiterCore>(kLimiterCeiling, -3.0f);
    testBatchIsAllOrNothing<LimiterCore>(kLimiterCeiling, -3.0f);
    testStateRoundTrip<LimiterCore>(kStateMagic);
    testStateRejectsCorruptBlobs<LimiterCore>();
    testProcessPerformsNoHeapAllocations<LimiterCore>(44100.0, 512, 16, [](LimiterCore& core) {
        loudio::ParamUpdate u[3] = { { kLimiterCeiling, -3.0f }, { kLimiterLookahead, 7.0f }, { kLimiterOversampling, (float)kLimiterOs4x } };
        core.setParameterValues(u, 3);
        core.setParameterValue(kLimiterInputGain, 12.0f);
    });
    testLimiterHardCeilingGuarantee();
    testLimiterTruePeak();
    testLimiterLatencyIsFixed();
    testLimiterReleaseTimeConstant();
    testLimiterStereoLink();
    testImpulseResponseMatchesGolden<LimiterCore>(LOUDIO_LIMITER_GOLDEN_DIR, kGoldenVariants, kGoldenVariantCount, kGoldenIrLength, kGoldenSampleRate);

    std::cout << "All Limiter DSP tests passed successfully!" << std::endl;
    return 0;
}
