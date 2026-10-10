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
#include "loudio/plugin/ParamTable.h"
#include "loudio/plugin/StateBlob.h"
#include "loudio/plugin/ParamQueue.h"

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

    std::cout << "All DSP component tests passed successfully!" << std::endl;
    return 0;
}
