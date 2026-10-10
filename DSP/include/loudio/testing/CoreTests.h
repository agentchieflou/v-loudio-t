#pragma once

/*
 * Generic test kit for a plugin core on the P1 contract (P4, #130).
 * Header-only, assert()-based like every test in this repo. A plugin's
 * DSPTests.cpp instantiates what applies to its core and adds its own
 * DSP-specific checks; tools/new_plugin.py generates exactly that.
 *
 * Usage (see VSTs/<Plugin>/src/DSPTests.cpp):
 *
 *   LOUDIO_DEFINE_ALLOCATION_HOOK();            // once, at namespace scope
 *   ...
 *   loudio::testing::testParamTableIndexIntegrity<MyCore>();
 *   loudio::testing::testDefaultsMatchTable<MyCore>();
 *   loudio::testing::testDirectSetAppliesImmediately<MyCore>(kMyGain, 0.5f);
 *   loudio::testing::testQueuedSetAppliesAfterProcess<MyCore>(kMyGain, 0.5f);
 *   loudio::testing::testBatchIsAllOrNothing<MyCore>(kMyGain, 0.5f);
 *   loudio::testing::testStateRoundTrip<MyCore>("MAGI");
 *   loudio::testing::testStateRejectsCorruptBlobs<MyCore>();
 *   loudio::testing::testProcessPerformsNoHeapAllocations<MyCore>(44100.0, 512, 16, edit);
 *   loudio::testing::testImpulseResponseMatchesGolden<MyCore>(dir, variants, n);
 *
 * Every function prints "Running ..." / "... Pass." like the hand-written
 * tests, so the CI log reads the same for every plugin.
 */

#include "loudio/plugin/ParamTable.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <string>
#include <vector>

namespace loudio {
namespace testing {

/* ---- allocation counting (for the zero-allocation process() test) ---- */

struct AllocationCounter {
    static inline bool armed = false;
    static inline long allocations = 0;
    static inline long deallocations = 0;
};

/* Replaces the global operator new/delete in the test executable so
 * every heap allocation is counted while AllocationCounter::armed. */
#define LOUDIO_DEFINE_ALLOCATION_HOOK() \
    void* operator new(std::size_t size) { \
        if (loudio::testing::AllocationCounter::armed) ++loudio::testing::AllocationCounter::allocations; \
        void* p = std::malloc(size ? size : 1); if (!p) throw std::bad_alloc(); return p; } \
    void* operator new[](std::size_t size) { \
        if (loudio::testing::AllocationCounter::armed) ++loudio::testing::AllocationCounter::allocations; \
        void* p = std::malloc(size ? size : 1); if (!p) throw std::bad_alloc(); return p; } \
    void operator delete(void* p) noexcept { if (loudio::testing::AllocationCounter::armed) ++loudio::testing::AllocationCounter::deallocations; std::free(p); } \
    void operator delete[](void* p) noexcept { if (loudio::testing::AllocationCounter::armed) ++loudio::testing::AllocationCounter::deallocations; std::free(p); } \
    void operator delete(void* p, std::size_t) noexcept { if (loudio::testing::AllocationCounter::armed) ++loudio::testing::AllocationCounter::deallocations; std::free(p); } \
    void operator delete[](void* p, std::size_t) noexcept { if (loudio::testing::AllocationCounter::armed) ++loudio::testing::AllocationCounter::deallocations; std::free(p); }

/* ---- parameter table and core contract ---- */

template <class Core>
void testParamTableIndexIntegrity() {
    std::cout << "Running testParamTableIndexIntegrity..." << std::endl;
    const ParamTable& t = Core::table();
    assert(t.count > 0);
    assert(t.indicesAreConsistent());
    for (int i = 0; i < t.count; ++i) {
        assert(t[i].id && t[i].id[0] != '\0');
        assert(std::strlen(t[i].id) < 256); /* StateBlob stores a u8 length */
        assert(t[i].minValue < t[i].maxValue);
        assert(t[i].defaultValue >= t[i].minValue && t[i].defaultValue <= t[i].maxValue);
        for (int j = 0; j < i; ++j) assert(std::strcmp(t[i].id, t[j].id) != 0); /* ids unique */
    }
    std::cout << "  table[i].index == i, ids unique and non-empty, defaults in range. Pass." << std::endl;
}

template <class Core>
void testDefaultsMatchTable() {
    std::cout << "Running testDefaultsMatchTable..." << std::endl;
    Core core;
    const ParamTable& t = Core::table();
    assert(core.getParameterCount() == t.count);
    for (int i = 0; i < t.count; ++i) assert(std::abs(core.getParameterValue(i) - t[i].defaultValue) < 1e-6f);
    ParamMeta meta;
    assert(core.getParameterInfo(0, &meta) && std::strcmp(meta.id, t[0].id) == 0);
    assert(!core.getParameterInfo(-1, &meta));
    assert(!core.getParameterInfo(t.count, &meta));
    assert(core.getParameterValue(-1) == 0.0f && core.getParameterValue(t.count) == 0.0f);
    std::cout << "  A fresh core's parameters match the table's defaults; bad indices are safe. Pass." << std::endl;
}

template <class Core>
void testDirectSetAppliesImmediately(int index, float value) {
    std::cout << "Running testDirectSetAppliesImmediately..." << std::endl;
    Core core;
    core.setParameterValueDirect(index, value);
    assert(std::abs(core.getParameterValue(index) - value) < 1e-6f);
    core.setParameterValueDirect(-1, 1.0f);
    core.setParameterValueDirect(Core::table().count, 1.0f);
    std::cout << "  setParameterValueDirect() applies at once; out-of-range indices are no-ops. Pass." << std::endl;
}

template <class Core>
void testQueuedSetAppliesAfterProcess(int index, float value, double sampleRate = 44100.0) {
    std::cout << "Running testQueuedSetAppliesAfterProcess..." << std::endl;
    Core core;
    core.prepare(sampleRate, 64);
    float before = core.getParameterValue(index);
    assert(std::abs(before - value) > 1e-6f); /* pick a value that differs from the default */
    assert(core.setParameterValue(index, value));
    assert(std::abs(core.getParameterValue(index) - before) < 1e-6f); /* not yet */
    float l[64] = {}, r[64] = {};
    core.process(l, r, 64);
    assert(std::abs(core.getParameterValue(index) - value) < 1e-6f);
    assert(!core.setParameterValue(-1, 1.0f));
    assert(!core.setParameterValue(Core::table().count, 1.0f));
    std::cout << "  setParameterValue() enqueues; the next process() applies it. Pass." << std::endl;
}

template <class Core>
void testBatchIsAllOrNothing(int index, float value, double sampleRate = 44100.0) {
    std::cout << "Running testBatchIsAllOrNothing..." << std::endl;
    Core core;
    core.prepare(sampleRate, 64);
    float l[64] = {}, r[64] = {};
    float before = core.getParameterValue(index);
    ParamUpdate bad[2] = { { index, value }, { Core::table().count, 0.0f } };
    assert(!core.setParameterValues(bad, 2));
    core.process(l, r, 64);
    assert(std::abs(core.getParameterValue(index) - before) < 1e-6f);
    ParamUpdate good[1] = { { index, value } };
    assert(core.setParameterValues(good, 1));
    core.process(l, r, 64);
    assert(std::abs(core.getParameterValue(index) - value) < 1e-6f);
    /* Fill the queue; a batch that doesn't fit whole is refused. */
    ParamUpdate one = { index, before };
    int queued = 0;
    while (core.setParameterValues(&one, 1)) ++queued;
    assert(queued > 0);
    assert(!core.setParameterValues(good, 1));
    core.process(l, r, 64);
    assert(std::abs(core.getParameterValue(index) - before) < 1e-6f);
    std::cout << "  setParameterValues() enqueues a whole batch or nothing. Pass." << std::endl;
}

template <class Core>
void testStateRoundTrip(const char magic[4]) {
    std::cout << "Running testStateRoundTrip..." << std::endl;
    const ParamTable& t = Core::table();
    Core core;
    for (int i = 0; i < t.count; ++i) {
        /* A value distinct from the default, inside the range. */
        float v = (t[i].flags & kParamBoolean) ? (t[i].defaultValue > 0.5f ? 0.0f : 1.0f)
                                               : (t[i].defaultValue > (t[i].minValue + t[i].maxValue) * 0.5f ? t[i].minValue : t[i].maxValue);
        core.setParameterValueDirect(i, v);
    }
    size_t required = core.getState(nullptr, 0);
    assert(required > 8);
    std::vector<unsigned char> blob(required);
    assert(core.getState(blob.data(), blob.size()) == required);
    assert(std::memcmp(blob.data(), magic, 4) == 0);
    assert(core.getState(blob.data(), required - 1) == 0);
    Core other;
    assert(other.setState(blob.data(), blob.size()));
    for (int i = 0; i < t.count; ++i) assert(std::abs(other.getParameterValue(i) - core.getParameterValue(i)) < 1e-6f);
    std::cout << "  getState()/setState() round-trips every parameter under magic '" << std::string(magic, 4) << "'. Pass." << std::endl;
}

template <class Core>
void testStateRejectsCorruptBlobs() {
    std::cout << "Running testStateRejectsCorruptBlobs..." << std::endl;
    Core source;
    std::vector<unsigned char> good(source.getState(nullptr, 0));
    source.getState(good.data(), good.size());
    Core core;
    const ParamTable& t = Core::table();
    std::vector<float> before((size_t)t.count);
    for (int i = 0; i < t.count; ++i) before[(size_t)i] = core.getParameterValue(i);
    auto untouched = [&]() { for (int i = 0; i < t.count; ++i) if (core.getParameterValue(i) != before[(size_t)i]) return false; return true; };
    for (size_t n = 0; n < good.size(); ++n) {
        if (core.setState(good.data(), n)) continue; /* a plugin may accept a legacy size; it must still be a valid load */
        assert(untouched());
    }
    std::vector<unsigned char> badMagic = good; badMagic[0] ^= 0xFF;
    assert(!core.setState(badMagic.data(), badMagic.size()) && untouched());
    std::vector<unsigned char> badVersion = good; badVersion[4] = 99;
    assert(!core.setState(badVersion.data(), badVersion.size()) && untouched());
    assert(!core.setState(nullptr, 0) && untouched());
    std::cout << "  Truncated, wrong-magic, wrong-version and null blobs are rejected without touching state. Pass." << std::endl;
}

/* `midStreamEdit(core)` runs once, part-way through, so coefficient
 * updates and smoother restarts are inside the counted region too. */
template <class Core, class Edit>
void testProcessPerformsNoHeapAllocations(double sampleRate, int blockSize, int blocks, Edit&& midStreamEdit) {
    std::cout << "Running testProcessPerformsNoHeapAllocations..." << std::endl;
    Core core;
    core.prepare(sampleRate, blockSize);
    std::vector<float> l((size_t)blockSize), r((size_t)blockSize);
    unsigned seed = 1;
    AllocationCounter::allocations = AllocationCounter::deallocations = 0;
    AllocationCounter::armed = true;
    for (int b = 0; b < blocks; ++b) {
        if (b == blocks / 4) midStreamEdit(core);
        for (int i = 0; i < blockSize; ++i) {
            seed = seed * 1664525u + 1013904223u;
            l[(size_t)i] = ((float)(seed >> 8) / 16777216.0f) * 0.5f - 0.25f;
            r[(size_t)i] = -l[(size_t)i];
        }
        core.process(l.data(), r.data(), blockSize);
    }
    AllocationCounter::armed = false;
    if (AllocationCounter::allocations || AllocationCounter::deallocations) {
        std::cerr << "  process() performed " << AllocationCounter::allocations << " allocations / "
                  << AllocationCounter::deallocations << " deallocations over " << blocks << " blocks" << std::endl;
    }
    assert(AllocationCounter::allocations == 0 && AllocationCounter::deallocations == 0);
    std::cout << "  " << blocks << " blocks of process() with a mid-stream edit performed zero heap allocations. Pass." << std::endl;
}

/* ---- golden impulse responses ---- */

template <class Core>
struct GoldenVariant {
    const char* name;            /* file is <dir>/ir_<name>.f32 */
    void (*configure)(Core& core); /* sets parameters before prepare(); may be nullptr */
};

template <class Core>
void renderImpulseResponse(const GoldenVariant<Core>& v, int length, double sampleRate, std::vector<float>& l, std::vector<float>& r) {
    Core core;
    if (v.configure) v.configure(core);
    core.prepare(sampleRate, length);
    l.assign((size_t)length, 0.0f);
    r.assign((size_t)length, 0.0f);
    l[0] = r[0] = 1.0f;
    core.process(l.data(), r.data(), length);
}

inline std::string goldenPath(const char* dir, const char* name) {
    return std::string(dir) + "/ir_" + name + ".f32";
}

/* File format: raw little-endian float32, `length` left samples then
 * `length` right samples. */
template <class Core>
void regenerateGoldenImpulseResponses(const char* dir, const GoldenVariant<Core>* variants, int count, int length, double sampleRate) {
    for (int i = 0; i < count; ++i) {
        std::vector<float> l, r;
        renderImpulseResponse(variants[i], length, sampleRate, l, r);
        std::string path = goldenPath(dir, variants[i].name);
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) { std::cerr << "Cannot write " << path << std::endl; std::exit(1); }
        std::fwrite(l.data(), sizeof(float), l.size(), f);
        std::fwrite(r.data(), sizeof(float), r.size(), f);
        std::fclose(f);
        std::cout << "  Wrote " << path << std::endl;
    }
}

/* Tolerance defaults to -60 dB: not bit-exact, to absorb libm differences
 * between the compiler that produced the goldens and MSVC. */
template <class Core>
void testImpulseResponseMatchesGolden(const char* dir, const GoldenVariant<Core>* variants, int count,
                                      int length, double sampleRate, float tolerance = 1e-3f, float minReferencePeak = 0.01f) {
    std::cout << "Running testImpulseResponseMatchesGolden..." << std::endl;
    float worst = 0.0f;
    for (int i = 0; i < count; ++i) {
        std::string path = goldenPath(dir, variants[i].name);
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) std::cerr << "  Missing golden IR " << path << " (run dsp_tests --regen-golden to create it)" << std::endl;
        assert(f);
        std::vector<float> refL((size_t)length), refR((size_t)length);
        size_t got = std::fread(refL.data(), sizeof(float), refL.size(), f);
        got += std::fread(refR.data(), sizeof(float), refR.size(), f);
        std::fclose(f);
        assert(got == 2 * (size_t)length);
        std::vector<float> l, r;
        renderImpulseResponse(variants[i], length, sampleRate, l, r);
        float refPeak = 0.0f, residual = 0.0f;
        for (int n = 0; n < length; ++n) {
            refPeak = std::max(refPeak, std::max(std::abs(refL[(size_t)n]), std::abs(refR[(size_t)n])));
            residual = std::max(residual, std::abs(l[(size_t)n] - refL[(size_t)n]));
            residual = std::max(residual, std::abs(r[(size_t)n] - refR[(size_t)n]));
        }
        assert(refPeak > minReferencePeak); /* a zeroed or truncated golden must not pass vacuously */
        if (residual >= tolerance) std::cerr << "  variant '" << variants[i].name << "' residual " << residual << std::endl;
        assert(residual < tolerance);
        worst = std::max(worst, residual);
    }
    std::cout << "  Impulse responses for " << count << " variant(s) null against the goldens within " << tolerance
              << " (worst residual " << worst << "). Pass." << std::endl;
}

} // namespace testing
} // namespace loudio
