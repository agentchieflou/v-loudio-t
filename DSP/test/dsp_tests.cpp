/*
 * Unit tests for the shared DSP components in DSP/include/loudio/dsp and
 * the plugin platform helpers in DSP/include/loudio/plugin (#127).
 * Plain assert()-based, JUCE-free and Windows-free, so they run on every
 * platform CI has (the Linux job gives ~30 s feedback). Plugin-level DSP
 * tests (ReverbCore, golden IRs, etc) stay with their plugin.
 */
#include "loudio/dsp/DelayLine.h"
#include "loudio/dsp/AllPassDiffuser.h"
#include "loudio/dsp/EarlyReflections.h"
#include "loudio/dsp/OnePoleLPF.h"
#include "loudio/dsp/Crossover.h"
#include "loudio/dsp/EnvelopeDetector.h"
#include "loudio/dsp/SimpleBiquad.h"
#include "loudio/dsp/SimpleFFT.h"
#include "loudio/dsp/FFTAnalyzer.h"
#include "loudio/dsp/SimpleSmoothedValue.h"
#include "loudio/dsp/Decibels.h"
#include "loudio/dsp/LevelDetector.h"
#include "loudio/dsp/GainComputer.h"
#include "loudio/dsp/Lookahead.h"
#include "loudio/dsp/SidechainHpf.h"
#include "loudio/dsp/Hysteresis.h"
#include "loudio/plugin/ParamTable.h"
#include "loudio/plugin/StateBlob.h"
#include "loudio/plugin/ParamQueue.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

/*
 * Regression test for the DelayLine::read() wrap-around precision bug the
 * golden-IR null test (#44) exposed. Feeds a known ramp for two full buffer
 * revolutions and checks every read against the ideal fractional delay,
 * for delays exactly on, just above and just below an integer. On the old
 * implementation the 882.00006 case returned 0 at the wrap sample instead
 * of ~0.5 -- for the impulse-response test that was THE sample, scaling
 * the entire wet path by ~6e-5.
 */
void testDelayLineFractionalReadAcrossWrap() {
    std::cout << "Running testDelayLineFractionalReadAcrossWrap..." << std::endl;

    const int size = 6617; /* ReverbCore's pre-delay buffer at 44.1 kHz */
    const float delays[] = { 882.0f, 882.00006f, 881.99994f, 1.0f, 1.5f, 6616.5f };
    for (float d : delays) {
        DelayLine line;
        line.prepare(size);
        int di = (int)d;
        float f = d - (float)di;
        std::vector<float> history;
        auto h = [&](int k) { return k >= 0 ? history[(size_t)k] : 0.0f; };

        for (int n = 0; n < 2 * size + 100; ++n) {
            float x = 0.5f + (float)(n % 1000) * 0.0004f; /* never zero, so a dropout can't hide */
            line.write(x);
            history.push_back(x);
            float got = line.read(d);
            /* write-then-read convention (as ReverbCore's pre-delay uses it):
             * read(D) after writing x[n] returns (1-f)*x[n+1-Di] + f*x[n-Di]. */
            float expected = (1.0f - f) * h(n + 1 - di) + f * h(n - di);
            if (std::abs(got - expected) >= 1e-5f) {
                std::cerr << "  delay " << d << " at n=" << n << ": got " << got << " expected " << expected << std::endl;
            }
            assert(std::abs(got - expected) < 1e-5f);
        }
    }

    std::cout << "  Fractional reads stay exact across two buffer revolutions for delays on/just above/just below an integer. Pass." << std::endl;
}

void testCrossoverSum() {
    std::cout << "Running testCrossoverSum..." << std::endl;

    ThreeBandCrossover crossover;
    crossover.prepare();
    crossover.setCrossoverFrequencies(44100.0f, 250.0f, 4000.0f);

    std::vector<float> input(1000);
    for (int i = 0; i < 1000; ++i) {
        input[i] = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
    }

    float inputEnergy = 0.0f;
    float outputEnergy = 0.0f;

    for (int i = 0; i < 1000; ++i) {
        float low = 0.0f, mid = 0.0f, high = 0.0f;
        crossover.process(input[i], low, mid, high);
        
        float sum = low + mid + high;
        inputEnergy += input[i] * input[i];
        outputEnergy += sum * sum;
    }

    std::cout << "  Input energy:  " << inputEnergy << std::endl;
    std::cout << "  Output energy: " << outputEnergy << std::endl;

    float ratio = outputEnergy / inputEnergy;
    std::cout << "  Energy preservation ratio: " << ratio << std::endl;
    assert(std::abs(ratio - 1.0f) < 0.05f);
    std::cout << "  Crossover network sums to flat (energy is conserved)! Pass." << std::endl;
}

void testDuckingEnvelope() {
    std::cout << "Running testDuckingEnvelope..." << std::endl;

    EnvelopeDetector detector;
    detector.prepare(44100.0);
    detector.setAttackRelease(10.0f, 200.0f);

    for (int i = 0; i < 1000; ++i) {
        float x = (i < 100) ? 1.0f : 0.0f;
        float db = detector.processDb(x);
        
        assert(db <= 0.0f);
        assert(db >= -100.0f);
    }
    std::cout << "  Ducking envelope tracking functions bounded correctly! Pass." << std::endl;
}

/* ------------------------------------------------------------------------
 * loudio/plugin (#127): ParamTable, StateBlob, ParamQueue. A tiny
 * three-parameter table stands in for a plugin's.
 * ---------------------------------------------------------------------- */

enum { kTGain = 0, kTBypass, kTMode, kTCount };
static const loudio::ParamMeta kTestParams[kTCount] = {
    { kTGain,   "gain",   "Gain",   -24.0f, 24.0f, 0.0f, 0.1f, 0 },
    { kTBypass, "bypass", "Bypass", 0.0f,   1.0f,  0.0f, 1.0f, loudio::kParamBoolean },
    { kTMode,   "mode",   "Mode",   0.0f,   3.0f,  1.0f, 1.0f, loudio::kParamStepped },
};
static const loudio::ParamTable kTestTable = { kTestParams, kTCount };
static const char kTestMagic[4] = { 'T', 'E', 'S', 'T' };

void testParamTableLookupSanitizeAndMapping() {
    std::cout << "Running testParamTableLookupSanitizeAndMapping..." << std::endl;

    assert(kTestTable.indicesAreConsistent());
    assert(kTestTable.valid(0) && kTestTable.valid(kTCount - 1));
    assert(!kTestTable.valid(-1) && !kTestTable.valid(kTCount));

    assert(kTestTable.findById("gain") == kTGain);
    assert(kTestTable.findById("mode") == kTMode);
    assert(kTestTable.findById("gai") == -1);     /* prefix is not a match */
    assert(kTestTable.findById("gainx") == -1);
    assert(kTestTable.findById("gainx", 4) == kTGain); /* explicit length: the blob reader's path */
    assert(kTestTable.findById(nullptr) == -1);

    assert(kTestTable.sanitize(kTGain, 100.0f) == 24.0f);
    assert(kTestTable.sanitize(kTGain, -100.0f) == -24.0f);
    assert(kTestTable.sanitize(kTGain, 3.5f) == 3.5f);
    assert(kTestTable.sanitize(kTGain, std::nanf("")) == 0.0f);
    assert(kTestTable.sanitize(kTMode, INFINITY) == 1.0f);

    /* Linear mapping round-trips; widget space is clamped to [0,1]. */
    for (int step = 0; step <= 20; ++step) {
        float w = step / 20.0f;
        float v = kTestTable.fromWidget(kTGain, w);
        assert(std::abs(v - (-24.0f + 48.0f * w)) < 1e-4f);
        assert(std::abs(kTestTable.toWidget(kTGain, v) - w) < 1e-5f);
    }
    assert(kTestTable.toWidget(kTGain, 1000.0f) == 1.0f);
    assert(kTestTable.toWidget(kTGain, -1000.0f) == 0.0f);

    /* Boolean thresholds; Stepped rounds to an integer step. */
    assert(kTestTable.fromWidget(kTBypass, 0.49f) == 0.0f);
    assert(kTestTable.fromWidget(kTBypass, 0.51f) == 1.0f);
    assert(kTestTable.fromWidget(kTMode, 0.4f) == 1.0f);  /* 1.2 -> 1 */
    assert(kTestTable.fromWidget(kTMode, 0.6f) == 2.0f);  /* 1.8 -> 2 */

    float defaults[kTCount];
    kTestTable.fillDefaults(defaults);
    assert(defaults[kTGain] == 0.0f && defaults[kTMode] == 1.0f);

    std::cout << "  findById (exact, by length), sanitize (clamp/NaN), linear/boolean/stepped widget mapping. Pass." << std::endl;
}

/* Hand-builds a v1 blob from (id, value) pairs, independent of
 * StateBlob::write, so the on-disk format is pinned, not just
 * self-consistency. */
static std::vector<unsigned char> buildBlob(const std::vector<std::pair<std::string, float>>& entries, uint16_t version = 1) {
    std::vector<unsigned char> b(kTestMagic, kTestMagic + 4);
    uint16_t count = (uint16_t)entries.size();
    b.insert(b.end(), (unsigned char*)&version, (unsigned char*)&version + 2);
    b.insert(b.end(), (unsigned char*)&count, (unsigned char*)&count + 2);
    for (const auto& e : entries) {
        b.push_back((unsigned char)e.first.size());
        b.insert(b.end(), e.first.begin(), e.first.end());
        b.insert(b.end(), (const unsigned char*)&e.second, (const unsigned char*)&e.second + 4);
    }
    return b;
}

void testStateBlobRoundTripAndFormat() {
    std::cout << "Running testStateBlobRoundTripAndFormat..." << std::endl;

    float values[kTCount] = { 6.5f, 1.0f, 3.0f };
    size_t required = loudio::StateBlob::write(kTestTable, kTestMagic, values, nullptr, 0);
    assert(required == loudio::StateBlob::requiredSize(kTestTable));
    assert(required == 8 + (1 + 4 + 4) + (1 + 6 + 4) + (1 + 4 + 4));

    std::vector<unsigned char> buf(required);
    assert(loudio::StateBlob::write(kTestTable, kTestMagic, values, buf.data(), buf.size()) == required);
    assert(loudio::StateBlob::write(kTestTable, kTestMagic, values, buf.data(), required - 1) == 0); /* undersized: nothing written */

    /* Byte-exact against the hand-built encoding. */
    auto expected = buildBlob({ { "gain", 6.5f }, { "bypass", 1.0f }, { "mode", 3.0f } });
    assert(buf == expected);

    float loaded[kTCount] = { -1.0f, -1.0f, -1.0f };
    assert(loudio::StateBlob::read(kTestTable, kTestMagic, buf.data(), buf.size(), loaded));
    for (int i = 0; i < kTCount; ++i) assert(loaded[i] == values[i]);

    std::cout << "  write() sizes, refuses undersized buffers, matches the documented byte layout, and read() round-trips. Pass." << std::endl;
}

void testStateBlobKeysOnIdAndSanitizes() {
    std::cout << "Running testStateBlobKeysOnIdAndSanitizes..." << std::endl;

    /* Reordered, an unknown id, one param missing. */
    auto blob = buildBlob({ { "mode", 2.0f }, { "notAParam", 42.0f }, { "gain", -3.0f } });
    float v[kTCount] = { 9.0f, 9.0f, 9.0f };
    assert(loudio::StateBlob::read(kTestTable, kTestMagic, blob.data(), blob.size(), v));
    assert(v[kTGain] == -3.0f);
    assert(v[kTMode] == 2.0f);
    assert(v[kTBypass] == 0.0f); /* missing -> default */

    auto wild = buildBlob({ { "gain", 1e9f }, { "mode", std::nanf("") } });
    assert(loudio::StateBlob::read(kTestTable, kTestMagic, wild.data(), wild.size(), v));
    assert(v[kTGain] == 24.0f);  /* clamped */
    assert(v[kTMode] == 1.0f);   /* NaN -> default */

    std::cout << "  Unknown ids skipped, missing ids defaulted, out-of-range clamped, NaN defaulted. Pass." << std::endl;
}

void testStateBlobRejectsCorruptInputAtomically() {
    std::cout << "Running testStateBlobRejectsCorruptInputAtomically..." << std::endl;

    auto good = buildBlob({ { "gain", 6.5f }, { "bypass", 1.0f }, { "mode", 3.0f } });
    const float sentinel[kTCount] = { 0.123f, 0.456f, 0.789f };
    float v[kTCount];

    /* Every truncation is rejected and leaves the values untouched. */
    for (size_t n = 0; n < good.size(); ++n) {
        std::memcpy(v, sentinel, sizeof(v));
        assert(!loudio::StateBlob::read(kTestTable, kTestMagic, good.data(), n, v));
        assert(std::memcmp(v, sentinel, sizeof(v)) == 0);
    }

    std::memcpy(v, sentinel, sizeof(v));
    auto wrongVersion = buildBlob({ { "gain", 6.5f } }, 2);
    assert(!loudio::StateBlob::read(kTestTable, kTestMagic, wrongVersion.data(), wrongVersion.size(), v));
    const char otherMagic[4] = { 'L', 'R', 'V', 'B' };
    assert(!loudio::StateBlob::read(kTestTable, otherMagic, good.data(), good.size(), v));
    assert(!loudio::StateBlob::read(kTestTable, kTestMagic, nullptr, 0, v));
    assert(std::memcmp(v, sentinel, sizeof(v)) == 0);

    /* An entry whose declared id length runs past the end is a truncation. */
    auto overrun = good;
    overrun[8] = 200;
    assert(!loudio::StateBlob::read(kTestTable, kTestMagic, overrun.data(), overrun.size(), v));
    assert(std::memcmp(v, sentinel, sizeof(v)) == 0);

    std::cout << "  Truncated, wrong-version, wrong-magic, null and overrunning blobs are rejected without touching values. Pass." << std::endl;
}

void testStateBlobLegacyRawArray() {
    std::cout << "Running testStateBlobLegacyRawArray..." << std::endl;

    /* A plugin that once wrote 2 raw floats (gain, bypass) can still load
     * them positionally when it says so; the third parameter defaults. */
    float legacy[2] = { 100.0f, 1.0f };
    float v[kTCount] = { 9.0f, 9.0f, 9.0f };
    assert(loudio::StateBlob::read(kTestTable, kTestMagic, legacy, sizeof(legacy), v, 2));
    assert(v[kTGain] == 24.0f); /* sanitized */
    assert(v[kTBypass] == 1.0f);
    assert(v[kTMode] == 1.0f);  /* default */

    /* Without opting in, the same bytes are just a bad blob. */
    float before[kTCount]; std::memcpy(before, v, sizeof(v));
    assert(!loudio::StateBlob::read(kTestTable, kTestMagic, legacy, sizeof(legacy), v));
    assert(std::memcmp(before, v, sizeof(v)) == 0);

    std::cout << "  Legacy positional arrays load only when the plugin opts in. Pass." << std::endl;
}

void testParamQueueAllOrNothingAndDrain() {
    std::cout << "Running testParamQueueAllOrNothingAndDrain..." << std::endl;

    loudio::ParamQueue<64> q; /* room for 8 updates */
    assert(q.capacityUpdates() == 8);
    float v[kTCount];
    kTestTable.fillDefaults(v);

    assert(q.push(kTestTable, kTGain, 3.0f));
    assert(!q.push(kTestTable, -1, 1.0f));
    assert(!q.push(kTestTable, kTCount, 1.0f));
    assert(v[kTGain] == 0.0f); /* nothing applied until drained */
    assert(q.drainInto(kTestTable, v) == 1);
    assert(v[kTGain] == 3.0f);
    assert(q.drainInto(kTestTable, v) == 0);

    /* A batch with one bad index enqueues nothing. */
    loudio::ParamUpdate bad[2] = { { kTGain, 5.0f }, { kTCount, 0.0f } };
    assert(!q.pushAll(kTestTable, bad, 2));
    assert(q.drainInto(kTestTable, v) == 0);
    assert(v[kTGain] == 3.0f);

    /* Last write wins within one drain. */
    loudio::ParamUpdate seq[3] = { { kTGain, 1.0f }, { kTGain, 2.0f }, { kTMode, 3.0f } };
    assert(q.pushAll(kTestTable, seq, 3));
    assert(q.drainInto(kTestTable, v) == 3);
    assert(v[kTGain] == 2.0f && v[kTMode] == 3.0f);

    /* Fill it, then a batch that doesn't fit whole is refused rather than
     * half-written; a single that fits is still accepted after draining. */
    int queued = 0;
    while (q.push(kTestTable, kTBypass, 1.0f)) ++queued;
    assert(queued == 8);
    loudio::ParamUpdate one[1] = { { kTGain, 7.0f } };
    assert(!q.pushAll(kTestTable, one, 1));
    assert(q.drainInto(kTestTable, v) == 8);
    assert(v[kTGain] == 2.0f); /* the refused batch never landed */
    assert(q.pushAll(kTestTable, one, 1));

    /* clear() discards without applying. */
    q.clear();
    assert(q.drainInto(kTestTable, v) == 0);
    assert(v[kTGain] == 2.0f);

    assert(q.pushAll(kTestTable, nullptr, 0));
    assert(!q.pushAll(kTestTable, nullptr, 1));

    std::cout << "  push/pushAll are all-or-nothing (bad index, full queue); drainInto applies in order; clear discards. Pass." << std::endl;
}

/* ------------------------------------------------------------------------
 * loudio/dsp dynamics blocks (P6, #132)
 * ---------------------------------------------------------------------- */

static bool near(float a, float b, float tol) { return std::abs(a - b) <= tol; }

void testDecibelsRoundTripAndFloor() {
    std::cout << "Running testDecibelsRoundTripAndFloor..." << std::endl;

    assert(near(loudio::dbToGain(0.0f), 1.0f, 1e-6f));
    assert(near(loudio::dbToGain(-6.0206f), 0.5f, 1e-4f));
    assert(near(loudio::gainToDb(2.0f), 6.0206f, 1e-3f));
    for (float db = -90.0f; db <= 24.0f; db += 3.7f) {
        assert(near(loudio::gainToDb(loudio::dbToGain(db)), db, 1e-3f));
    }
    assert(loudio::gainToDb(0.0f) == loudio::kMinusInfinityDb);
    assert(loudio::gainToDb(-1.0f) == loudio::kMinusInfinityDb);
    assert(loudio::gainToDb(1e-9f) == loudio::kMinusInfinityDb); /* floored, not -180 */
    assert(loudio::dbToGain(loudio::kMinusInfinityDb) == 0.0f);

    std::cout << "  dB<->gain round-trips; silence floors at -100 dB. Pass." << std::endl;
}

void testGainComputerStaticCurve() {
    std::cout << "Running testGainComputerStaticCurve..." << std::endl;

    loudio::GainComputer gc;
    gc.setMode(loudio::GainComputer::Mode::Compressor);
    gc.setThreshold(-30.0f);
    gc.setRatio(4.0f);
    gc.setKneeWidth(0.0f);

    /* The spike's acceptance point: -20 dB in, 4:1 above -30 -> -27.5 out. */
    assert(near(gc.outputDb(-20.0f), -27.5f, 1e-4f));
    assert(near(gc.gainReductionDb(-20.0f), 7.5f, 1e-4f));
    assert(gc.gainReductionDb(-40.0f) == 0.0f);   /* below threshold: untouched */
    assert(near(gc.outputDb(-30.0f), -30.0f, 1e-5f)); /* exactly at threshold */

    /* Soft knee: continuous at both knee edges, half-ratio slope at T, and
     * the knee curve lies between the two hard-knee lines. */
    gc.setKneeWidth(10.0f);
    assert(near(gc.outputDb(-35.0f), -35.0f, 1e-4f));           /* lower edge: y = x */
    assert(near(gc.outputDb(-25.0f), -30.0f + 5.0f / 4.0f, 1e-4f)); /* upper edge: hard-knee line */
    float atT = gc.outputDb(-30.0f);
    assert(atT < -30.0f && atT > -30.0f - 5.0f * 0.75f);
    assert(near(atT, -30.0f + (0.25f - 1.0f) * 25.0f / 20.0f, 1e-4f)); /* -30.9375 */
    for (float x = -36.0f; x <= -24.0f; x += 0.5f) {
        float y = gc.outputDb(x);
        float hard = x < -30.0f ? x : -30.0f + (x + 30.0f) / 4.0f;
        assert(y <= x + 1e-5f);
        assert(y <= hard + 1e-5f || near(y, hard, 1e-4f)); /* knee never exceeds hard-knee output */
    }

    /* Limiter: infinite ratio pins the output at the threshold. */
    gc.setKneeWidth(0.0f);
    gc.setRatio(loudio::GainComputer::kInfiniteRatio);
    gc.setThreshold(-1.0f);
    assert(near(gc.outputDb(+6.0f), -1.0f, 1e-4f));
    assert(near(gc.gainReductionDb(+6.0f), 7.0f, 1e-4f));

    /* Expander / gate: 1:4 below -40, floor at 20 dB of range. */
    gc.setMode(loudio::GainComputer::Mode::Expander);
    gc.setThreshold(-40.0f);
    gc.setRatio(4.0f);
    gc.setRange(20.0f);
    assert(gc.gainReductionDb(-30.0f) == 0.0f);           /* above threshold */
    assert(near(gc.outputDb(-42.0f), -48.0f, 1e-4f));     /* 2 dB under -> 8 dB under */
    assert(near(gc.gainReductionDb(-42.0f), 6.0f, 1e-4f));
    assert(near(gc.gainReductionDb(-60.0f), 20.0f, 1e-4f)); /* would be 60, clamped by range */
    gc.setKneeWidth(6.0f);
    assert(near(gc.outputDb(-37.0f), -37.0f, 1e-4f));     /* upper knee edge: untouched */
    assert(near(gc.outputDb(-43.0f), -40.0f + (-3.0f) * 4.0f, 1e-4f)); /* lower edge: hard line */
    assert(gc.outputDb(-40.0f) < -40.0f);                 /* knee bites a little at T */

    std::cout << "  Compressor hard/soft knee, limiter and ranged expander curves match the formulas. Pass." << std::endl;
}

void testLevelDetectorTimeConstantsHoldAndRms() {
    std::cout << "Running testLevelDetectorTimeConstantsHoldAndRms..." << std::endl;

    const double fs = 48000.0;
    loudio::LevelDetector det;
    det.setAttackRelease(10.0f, 100.0f);
    det.prepare(fs);

    /* Step 0 -> 1: 63.2% after one attack time. */
    const int attackSamples = 480;
    float env = 0.0f;
    for (int n = 0; n < attackSamples; ++n) env = det.process(1.0f);
    assert(near(env, 1.0f - std::exp(-1.0f), 0.01f));
    for (int n = 0; n < 48000; ++n) env = det.process(1.0f);
    assert(near(env, 1.0f, 1e-3f));

    /* Step 1 -> 0: 36.8% after one release time. */
    for (int n = 0; n < 4800; ++n) env = det.process(0.0f);
    assert(near(env, std::exp(-1.0f), 0.01f));

    /* Hold freezes the envelope before release starts, then releases. */
    det.setHold(5.0f); /* 240 samples */
    det.clear();
    for (int n = 0; n < 48000; ++n) det.process(1.0f);
    float held = det.process(0.0f);
    for (int n = 0; n < 238; ++n) {
        float v = det.process(0.0f);
        assert(v == held);
    }
    for (int n = 0; n < 10; ++n) env = det.process(0.0f);
    assert(env < held);

    /* A new peak during the hold restarts it. */
    det.setHold(5.0f);
    det.clear();
    for (int n = 0; n < 48000; ++n) det.process(1.0f);
    for (int n = 0; n < 200; ++n) det.process(0.0f);
    for (int n = 0; n < 48000; ++n) det.process(1.0f);
    held = det.process(0.0f);
    for (int n = 0; n < 230; ++n) assert(det.process(0.0f) == held);

    /* RMS of a full-scale sine reads 0.707 (-3.01 dB) once settled. */
    loudio::LevelDetector rms;
    rms.setMode(loudio::LevelDetector::Mode::Rms);
    rms.setAttackRelease(50.0f, 50.0f);
    rms.prepare(fs);
    float level = 0.0f;
    for (int n = 0; n < 48000; ++n) level = rms.process(std::sin(2.0f * (float)M_PI * 1000.0f * (float)n / (float)fs));
    assert(near(level, 0.7071f, 0.01f));
    assert(near(rms.currentLevel(), level, 1e-6f));
    assert(near(loudio::gainToDb(level), -3.01f, 0.15f));

    /* Silence reads the floor, not NaN. */
    rms.clear();
    assert(rms.processDb(0.0f) == loudio::kMinusInfinityDb);

    std::cout << "  Attack 63%, release 37%, hold freezes/restarts, RMS sine = -3 dB. Pass." << std::endl;
}

void testLookaheadDelaysByReportedLatency() {
    std::cout << "Running testLookaheadDelaysByReportedLatency..." << std::endl;

    for (int delay : { 0, 1, 7, 64, 256 }) {
        loudio::Lookahead la;
        la.setDelaySamples(delay);
        la.prepare(256);
        assert(la.latencySamples() == delay);
        int arrived = -1;
        for (int n = 0; n < 600; ++n) {
            float out = la.process(n == 0 ? 1.0f : 0.0f);
            if (out != 0.0f) {
                assert(arrived == -1); /* exactly one non-zero sample */
                assert(out == 1.0f);
                arrived = n;
            }
        }
        assert(arrived == delay);
    }

    /* Clamped to the prepared maximum. */
    loudio::Lookahead la;
    la.prepare(16);
    la.setDelaySamples(1000);
    assert(la.latencySamples() == 16);

    std::cout << "  An impulse arrives exactly latencySamples() later for delays 0..256. Pass." << std::endl;
}

static float steadyStateAmplitude(loudio::SidechainHpf& f, float hz, double fs) {
    float peak = 0.0f;
    for (int n = 0; n < (int)fs; ++n) {
        float y = f.process(std::sin(2.0f * (float)M_PI * hz * (float)n / (float)fs));
        if (n > (int)fs / 2 && std::abs(y) > peak) peak = std::abs(y);
    }
    return peak;
}

void testSidechainHpfAttenuatesBass() {
    std::cout << "Running testSidechainHpfAttenuatesBass..." << std::endl;

    const double fs = 48000.0;
    loudio::SidechainHpf hpf;
    hpf.prepare(fs);
    hpf.setCutoff(200.0f);

    float at50 = steadyStateAmplitude(hpf, 50.0f, fs);
    hpf.clear();
    float at200 = steadyStateAmplitude(hpf, 200.0f, fs);
    hpf.clear();
    float at2k = steadyStateAmplitude(hpf, 2000.0f, fs);
    assert(loudio::gainToDb(at50) < -20.0f);              /* 2 octaves below: ~ -24 dB */
    assert(near(loudio::gainToDb(at200), -3.0f, 0.3f));   /* Butterworth: -3 dB at cutoff */
    assert(near(loudio::gainToDb(at2k), 0.0f, 0.1f));     /* passband */

    /* Cutoff 0 bypasses exactly. */
    hpf.setCutoff(0.0f);
    assert(hpf.process(0.37f) == 0.37f);

    /* The new biquad makers more generally: low-pass mirrors, notch nulls. */
    SimpleBiquad lp; lp.makeLowPass(fs, 200.0f, 0.70710678f);
    SimpleBiquad notch; notch.makeNotch(fs, 1000.0f, 2.0f);
    float lpAt2k = 0.0f, notchAt1k = 0.0f, notchAt3k = 0.0f;
    for (int n = 0; n < 48000; ++n) {
        float y1 = lp.processSample(std::sin(2.0f * (float)M_PI * 2000.0f * (float)n / (float)fs));
        float y2 = notch.processSample(std::sin(2.0f * (float)M_PI * 1000.0f * (float)n / (float)fs));
        if (n > 24000) { lpAt2k = std::max(lpAt2k, std::abs(y1)); notchAt1k = std::max(notchAt1k, std::abs(y2)); }
    }
    notch.reset();
    for (int n = 0; n < 48000; ++n) {
        float y = notch.processSample(std::sin(2.0f * (float)M_PI * 3000.0f * (float)n / (float)fs));
        if (n > 24000) notchAt3k = std::max(notchAt3k, std::abs(y));
    }
    assert(loudio::gainToDb(lpAt2k) < -35.0f);
    assert(loudio::gainToDb(notchAt1k) < -40.0f);
    assert(loudio::gainToDb(notchAt3k) > -1.5f);

    std::cout << "  HPF: -3 dB at cutoff, >20 dB down two octaves below, flat above; LP/notch makers behave. Pass." << std::endl;
}

void testHysteresisHasNoChatter() {
    std::cout << "Running testHysteresisHasNoChatter..." << std::endl;

    loudio::Hysteresis h;
    h.setThresholds(-40.0f, -46.0f);
    assert(!h.open());

    /* A level wobbling +-1 dB around the open threshold toggles a single
     * threshold every sample; with 6 dB of hysteresis it opens once and
     * stays open. */
    int transitions = 0;
    bool prev = h.open();
    for (int n = 0; n < 1000; ++n) {
        bool now = h.process(-40.0f + ((n & 1) ? 1.0f : -1.0f));
        if (now != prev) ++transitions;
        prev = now;
    }
    assert(transitions == 1 && h.open());

    /* Falls below close: closes once; wobbling around close stays closed. */
    assert(!h.process(-47.0f));
    transitions = 0; prev = false;
    for (int n = 0; n < 1000; ++n) {
        bool now = h.process(-46.0f + ((n & 1) ? 0.5f : -0.5f));
        if (now != prev) ++transitions;
        prev = now;
    }
    assert(transitions == 0 && !h.open());

    /* close > open is clamped to open. */
    h.setThresholds(-30.0f, -20.0f);
    h.reset(true);
    assert(h.process(-31.0f) == false);
    assert(h.process(-29.0f) == true);

    std::cout << "  Opens above open, closes below close, no toggling in between. Pass." << std::endl;
}

int main() {
    std::cout << "==============================" << std::endl;
    std::cout << "Starting Loudio DSP component tests" << std::endl;
    std::cout << "==============================" << std::endl;

    testDelayLineFractionalReadAcrossWrap();
    testCrossoverSum();
    testDuckingEnvelope();
    testParamTableLookupSanitizeAndMapping();
    testStateBlobRoundTripAndFormat();
    testStateBlobKeysOnIdAndSanitizes();
    testStateBlobRejectsCorruptInputAtomically();
    testStateBlobLegacyRawArray();
    testParamQueueAllOrNothingAndDrain();
    testDecibelsRoundTripAndFloor();
    testGainComputerStaticCurve();
    testLevelDetectorTimeConstantsHoldAndRms();
    testLookaheadDelaysByReportedLatency();
    testSidechainHpfAttenuatesBass();
    testHysteresisHasNoChatter();

    std::cout << "All DSP component tests passed successfully!" << std::endl;
    return 0;
}
