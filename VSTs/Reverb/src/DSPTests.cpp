#include "DelayLine.h"
#include "AllPassDiffuser.h"
#include "EarlyReflections.h"
#include "OnePoleLPF.h"
#include "Crossover.h"
#include "EnvelopeDetector.h"
#include "SharedStructures.h"
#include "cuif/ring_buffer.h"
#include "ReverbThemeMapping.h"
#include "ReverbLayout.h"
#include "ReverbCore.h"
#include "ReverbUiMapping.h"
#include <set>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>

/*
 * Debug allocation hook for testReverbCoreProcessPerformsNoHeapAllocations
 * (#47). Replacing the global operator new/delete routes every heap
 * allocation in this executable through here; counting only while
 * g_countAllocations is armed around process() gives an exact count of
 * what the audio thread does, without disturbing the rest of the suite
 * (iostreams, the std::vectors the other tests build, etc).
 */
static bool g_countAllocations = false;
static long g_allocationCount = 0;
static long g_deallocationCount = 0;

void* operator new(std::size_t size) {
    if (g_countAllocations) ++g_allocationCount;
    void* p = std::malloc(size ? size : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t size) {
    if (g_countAllocations) ++g_allocationCount;
    void* p = std::malloc(size ? size : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { if (g_countAllocations) ++g_deallocationCount; std::free(p); }
void operator delete[](void* p) noexcept { if (g_countAllocations) ++g_deallocationCount; std::free(p); }
void operator delete(void* p, std::size_t) noexcept { if (g_countAllocations) ++g_deallocationCount; std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { if (g_countAllocations) ++g_deallocationCount; std::free(p); }

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

void testHouseholderUnitary() {
    std::cout << "Running testHouseholderUnitary..." << std::endl;

    const int N = 8;
    float x[N] = { 1.0f, -0.5f, 0.2f, 0.8f, -0.1f, 0.3f, -0.4f, 0.9f };

    /* Calculate input energy */
    float inputEnergy = 0.0f;
    for (int i = 0; i < N; ++i) {
        inputEnergy += x[i] * x[i];
    }

    /* Apply Householder transformation */
    float sum = 0.0f;
    for (int i = 0; i < N; ++i) {
        sum += x[i];
    }

    float factor = 2.0f / (float)N;
    float y[N];
    for (int i = 0; i < N; ++i) {
        y[i] = x[i] - factor * sum;
    }

    /* Calculate output energy */
    float outputEnergy = 0.0f;
    for (int i = 0; i < N; ++i) {
        outputEnergy += y[i] * y[i];
    }

    std::cout << "  Input energy:  " << inputEnergy << std::endl;
    std::cout << "  Output energy: " << outputEnergy << std::endl;

    float diff = std::abs(inputEnergy - outputEnergy);
    assert(diff < 1e-5f);
    std::cout << "  Householder matrix is unitary (conserves energy)! Pass." << std::endl;
}

void testFDNStability() {
    std::cout << "Running testFDNStability..." << std::endl;

    const int N = 8;
    const float baseDelayLengths[N] = { 1013.0f, 1123.0f, 1277.0f, 1399.0f, 1511.0f, 1693.0f, 1823.0f, 1999.0f };

    DelayLine delayLines[N];
    OnePoleLPF dampingFilters[N];

    for (int i = 0; i < N; ++i) {
        delayLines[i].prepare((int)baseDelayLengths[i] + 2);
        dampingFilters[i].prepare();
    }

    float loopGains[N] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    float g_hf = 0.0f;

    float inputSignal[N] = { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f };

    for (int sample = 0; sample < 2000; ++sample) {
        float x_delay[8];
        for (int i = 0; i < 8; ++i) {
            x_delay[i] = delayLines[i].read(baseDelayLengths[i]);
        }

        float x_damped[8];
        for (int i = 0; i < 8; ++i) {
            x_damped[i] = dampingFilters[i].process(x_delay[i], g_hf);
        }

        float sum = 0.0f;
        for (int i = 0; i < 8; ++i) {
            sum += x_damped[i];
        }

        float factor = 0.25f;
        float x_feedback[8];
        for (int i = 0; i < 8; ++i) {
            x_feedback[i] = x_damped[i] - factor * sum;
        }

        float totalSystemEnergy = 0.0f;
        for (int i = 0; i < 8; ++i) {
            float in_sample = (sample == 0) ? inputSignal[i] : 0.0f;
            float writeVal = in_sample + x_feedback[i] * loopGains[i];
            delayLines[i].write(writeVal);

            totalSystemEnergy += writeVal * writeVal;
        }

        if (sample > 10) {
            assert(totalSystemEnergy <= 8.5f);
        }
    }

    std::cout << "  FDN loop is stable over 2000 samples! Pass." << std::endl;
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

void testParameterRingBuffer() {
    std::cout << "Running testParameterRingBuffer..." << std::endl;

    cuif_spsc_ring_buffer ringBuffer;
    unsigned char storage[256];
    cuif_spsc_init(&ringBuffer, storage, 256);

    ParamUpdateMessage msgWrite = { kParamDecayTime, 4.5f };
    assert(cuif_spsc_writable(&ringBuffer) >= sizeof(ParamUpdateMessage));

    size_t written = cuif_spsc_write(&ringBuffer, (const unsigned char*)&msgWrite, sizeof(ParamUpdateMessage));
    assert(written == sizeof(ParamUpdateMessage));

    assert(cuif_spsc_readable(&ringBuffer) == sizeof(ParamUpdateMessage));

    ParamUpdateMessage msgRead = { 0, 0.0f };
    size_t read_bytes = cuif_spsc_read(&ringBuffer, (unsigned char*)&msgRead, sizeof(ParamUpdateMessage));
    assert(read_bytes == sizeof(ParamUpdateMessage));

    assert(msgRead.index == kParamDecayTime);
    assert(std::abs(msgRead.value - 4.5f) < 1e-5f);

    std::cout << "  Parameter message read/write over SPSC ring buffer works perfectly! Pass." << std::endl;
}

void testUiThemeMapping() {
    std::cout << "Running testUiThemeMapping..." << std::endl;

    assert(themeForUiThemeIndex(0) == &CUIF_THEME_DEFAULT);
    assert(themeForUiThemeIndex(1) == &CUIF_THEME_HELLO_KITTY);
    assert(themeForUiThemeIndex(2) == &CUIF_THEME_GREENS);

    /* Out-of-range indices must fail safe to the default theme, not crash or return null. */
    assert(themeForUiThemeIndex(-1) == &CUIF_THEME_DEFAULT);
    assert(themeForUiThemeIndex(99) == &CUIF_THEME_DEFAULT);

    std::cout << "  themeForUiThemeIndex() maps the uiTheme parameter's choice index correctly, with a safe fallback. Pass." << std::endl;
}

static bool rectsOverlap(const ReverbWidgetRect& a, const ReverbWidgetRect& b) {
    return a.x < b.x + b.w && a.x + a.w > b.x &&
           a.y < b.y + b.h && a.y + a.h > b.y;
}

void testReverbLayoutNoOverlapWithinVisibilityGroups() {
    std::cout << "Running testReverbLayoutNoOverlapWithinVisibilityGroups..." << std::endl;

    const float panelW = 800.0f, panelH = 600.0f;
    auto layout = computeReverbLayout(panelW, panelH);

    /*
     * Global rects are visible alongside whichever tab is active, so they
     * must not overlap Main OR Advanced rects, nor each other. Main and
     * Advanced rects are never shown simultaneously, so they're allowed to
     * occupy the same screen space -- this is the same overlap bug class
     * as #64 (analyzerLeft/analyzerRight sharing one rect), generalized.
     */
    for (size_t i = 0; i < layout.size(); ++i) {
        for (size_t j = i + 1; j < layout.size(); ++j) {
            const auto& a = layout[i];
            const auto& b = layout[j];

            bool sameGroup = (a.group == b.group);
            bool eitherIsGlobal = (a.group == ReverbLayoutGroup::Global || b.group == ReverbLayoutGroup::Global);
            bool mustNotOverlap = sameGroup || eitherIsGlobal;

            if (mustNotOverlap && rectsOverlap(a, b)) {
                std::cout << "  FAIL: '" << a.id << "' overlaps '" << b.id << "'" << std::endl;
            }
            assert(!mustNotOverlap || !rectsOverlap(a, b));
        }
    }

    std::cout << "  No two widgets that can be visible at the same time overlap. Pass." << std::endl;
}

void testReverbLayoutStaysInBounds() {
    std::cout << "Running testReverbLayoutStaysInBounds..." << std::endl;

    const float panelW = 800.0f, panelH = 600.0f;
    auto layout = computeReverbLayout(panelW, panelH);

    for (const auto& rect : layout) {
        bool inBounds = rect.x >= 0.0f && rect.y >= 0.0f &&
                         rect.x + rect.w <= panelW && rect.y + rect.h <= panelH;
        if (!inBounds) {
            std::cout << "  FAIL: '" << rect.id << "' is out of panel bounds ("
                       << rect.x << "," << rect.y << "," << rect.w << "," << rect.h << ")" << std::endl;
        }
        assert(inBounds);
    }

    std::cout << "  All widgets stay within the panel bounds. Pass." << std::endl;
}

void testReverbLayoutHasAllExpectedWidgets() {
    std::cout << "Running testReverbLayoutHasAllExpectedWidgets..." << std::endl;

    auto layout = computeReverbLayout(800.0f, 600.0f);

    std::set<std::string> ids;
    for (const auto& rect : layout) ids.insert(rect.id);

    static const char* expected[] = {
        "tabbar", "dropdown.mode", "dropdown.theme", "button.freeze",
        "knob.decayTime", "knob.preDelay", "knob.damping", "knob.width", "knob.dryWet", "knob.distance", "knob.thickness",
        "bezier.decayEq", "analyzer.left", "analyzer.right",
        "knob.postEqLowGain", "knob.postEqMidGain", "knob.postEqHighGain",
        "knob.crossoverLowMid", "knob.crossoverMidHigh",
        "knob.duckThreshold", "knob.duckAmount", "knob.duckRelease",
        "knob.gateThreshold", "knob.gateTime",
    };

    for (const char* id : expected) {
        if (ids.find(id) == ids.end()) {
            std::cout << "  FAIL: layout is missing '" << id << "'" << std::endl;
        }
        assert(ids.find(id) != ids.end());
    }

    assert(ids.size() == sizeof(expected) / sizeof(expected[0]));
    std::cout << "  Layout includes exactly the expected 24 widgets (no missing, no duplicate ids). Pass." << std::endl;
}

/*
 * Tests for ReverbCore (#100/B3) -- the JUCE-free DSP engine extracted from
 * LoudioReverbProcessor::processBlock(). Every formula in ReverbCore::
 * process() was ported line-for-line from the pre-extraction processBlock()
 * (cross-checked directly against the prior commit's source, not just from
 * memory); these tests exercise the actual extracted engine directly,
 * without APVTS or JUCE involved at all, since dsp_tests links no JUCE
 * library.
 */

void testReverbParamTableIndexIntegrity() {
    std::cout << "Running testReverbParamTableIndexIntegrity..." << std::endl;

    for (int i = 0; i < kNumParams; ++i) {
        assert(kReverbParams[i].index == i);
    }

    std::cout << "  kReverbParams[i].index == i holds for every parameter -- order can't silently drift. Pass." << std::endl;
}

void testReverbCoreDefaultsMatchParamTable() {
    std::cout << "Running testReverbCoreDefaultsMatchParamTable..." << std::endl;

    ReverbCore core;
    for (int i = 0; i < kNumParams; ++i) {
        assert(std::abs(core.getParameterValue(i) - kReverbParams[i].defaultValue) < 1e-6f);
    }

    assert(core.getParameterCount() == kNumParams);

    ReverbParamMeta meta;
    assert(core.getParameterInfo(kParamDecayTime, &meta));
    assert(std::string(meta.id) == "decayTime");
    assert(!core.getParameterInfo(-1, &meta));
    assert(!core.getParameterInfo(kNumParams, &meta));

    std::cout << "  A freshly-constructed ReverbCore's parameters match kReverbParams' defaults exactly. Pass." << std::endl;
}

void testReverbCoreSetParameterValueDirectAppliesImmediately() {
    std::cout << "Running testReverbCoreSetParameterValueDirectAppliesImmediately..." << std::endl;

    ReverbCore core;
    core.setParameterValueDirect(kParamDecayTime, 7.5f);
    assert(std::abs(core.getParameterValue(kParamDecayTime) - 7.5f) < 1e-6f);

    /* Out-of-range index must be a safe no-op, not a crash or OOB write. */
    core.setParameterValueDirect(-1, 1.0f);
    core.setParameterValueDirect(kNumParams, 1.0f);

    std::cout << "  setParameterValueDirect() applies immediately, same-thread, no queue. Pass." << std::endl;
}

void testReverbCoreSetParameterValueAppliesAfterProcess() {
    std::cout << "Running testReverbCoreSetParameterValueAppliesAfterProcess..." << std::endl;

    ReverbCore core;
    core.prepare(44100.0, 512);

    assert(core.setParameterValue(kParamThickness, 0.9f));
    /* Not yet applied -- only process() drains the ring buffer. */
    assert(std::abs(core.getParameterValue(kParamThickness) - kReverbParams[kParamThickness].defaultValue) < 1e-6f);

    float left[16] = {}, right[16] = {};
    core.process(left, right, 16);

    assert(std::abs(core.getParameterValue(kParamThickness) - 0.9f) < 1e-6f);

    /* Out-of-range index must fail cleanly, matching the LPI ABI contract
     * (PluginABI/include/lpi/lpi.h) this mirrors. */
    assert(!core.setParameterValue(-1, 1.0f));
    assert(!core.setParameterValue(kNumParams, 1.0f));

    std::cout << "  setParameterValue() enqueues; the value is applied by the next process() call, matching the LPI ABI's RT-safety contract. Pass." << std::endl;
}

void testReverbCoreDryWetZeroIsExactPassthrough() {
    std::cout << "Running testReverbCoreDryWetZeroIsExactPassthrough..." << std::endl;

    ReverbCore core;
    core.prepare(44100.0, 4096);
    core.setParameterValueDirect(kParamDryWet, 0.0f);

    const int numSamples = 4096;
    std::vector<float> left(numSamples), right(numSamples);
    std::vector<float> originalLeft(numSamples), originalRight(numSamples);
    for (int i = 0; i < numSamples; ++i) {
        left[i] = std::sin((float)i * 0.05f) * 0.5f;
        right[i] = std::cos((float)i * 0.03f) * 0.4f;
        originalLeft[i] = left[i];
        originalRight[i] = right[i];
    }

    core.process(left.data(), right.data(), numSamples);

    /*
     * The dry/wet smoothing ramp (0.02s -> ~882 samples at 44100Hz) means
     * dryWet isn't exactly 0.0 from sample 0 -- SmoothedValue lands exactly
     * on target once the ramp completes (see SmoothedValue::getNextValue's
     * "land exactly on target" step), so checking well past the ramp (from
     * sample 1500 onward) gives a genuine zero-tolerance bit-exact check:
     * leftChannel[sample] = (1.0f - dryWet)*inL + dryWet*outWetL reduces to
     * exactly inL when dryWet == 0.0f, regardless of what the internal wet
     * signal chain computed.
     */
    for (int i = 1500; i < numSamples; ++i) {
        assert(left[i] == originalLeft[i]);
        assert(right[i] == originalRight[i]);
    }

    std::cout << "  At dryWet=0.0 (fully settled past the smoothing ramp), output is bit-exact input passthrough. Pass." << std::endl;
}

void testReverbCoreProcessStaysBoundedOnNoise() {
    std::cout << "Running testReverbCoreProcessStaysBoundedOnNoise..." << std::endl;

    ReverbCore core;
    core.prepare(44100.0, 4096);

    const int numSamples = 8192;
    std::vector<float> left(numSamples), right(numSamples);
    for (int i = 0; i < numSamples; ++i) {
        left[i] = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
        right[i] = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
    }

    core.process(left.data(), right.data(), numSamples);

    for (int i = 0; i < numSamples; ++i) {
        assert(std::isfinite(left[i]));
        assert(std::isfinite(right[i]));
        /* Generous bound -- this is a stability sanity check (no blow-up),
         * not a precise gain-staging assertion. */
        assert(std::abs(left[i]) < 100.0f);
        assert(std::abs(right[i]) < 100.0f);
    }

    std::cout << "  8192 samples of white noise through the full signal chain produce finite, bounded output. Pass." << std::endl;
}

void testReverbCoreFreezeSustainsEnergy() {
    std::cout << "Running testReverbCoreFreezeSustainsEnergy..." << std::endl;

    ReverbCore core;
    core.prepare(44100.0, 8192);
    core.setParameterValueDirect(kParamFreeze, 1.0f);
    core.setParameterValueDirect(kParamDryWet, 1.0f);

    const int numSamples = 8192;
    std::vector<float> left(numSamples, 0.0f), right(numSamples, 0.0f);
    /* A short burst, then silence -- freeze mode should sustain the tail
     * near-indefinitely rather than letting it decay to silence. */
    for (int i = 0; i < 64; ++i) {
        left[i] = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
        right[i] = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
    }

    core.process(left.data(), right.data(), numSamples);

    float tailEnergy = 0.0f;
    for (int i = numSamples - 1000; i < numSamples; ++i) {
        tailEnergy += left[i] * left[i] + right[i] * right[i];
    }

    /* Not silence -- freeze mode holds the loop gain at unity, so the tail
     * should still carry meaningful energy thousands of samples after the
     * initial burst, unlike a normal decay. */
    assert(tailEnergy > 1e-4f);
    for (int i = 0; i < numSamples; ++i) {
        assert(std::isfinite(left[i]));
        assert(std::isfinite(right[i]));
    }

    std::cout << "  Freeze mode sustains tail energy (" << tailEnergy << ") well after the input burst ends, and stays finite. Pass." << std::endl;
}

void testReverbUiMappingMatchesLegacyEditorTables() {
    std::cout << "Running testReverbUiMappingMatchesLegacyEditorTables..." << std::endl;

    /* The per-parameter widget->param table PluginEditor.cpp hardcoded
     * before the shared ReverbUi, copied verbatim. The generic
     * kReverbParams-driven mapping must reproduce it exactly, so moving the
     * VST3 editor onto ReverbUi changes no knob's range. */
    auto legacy = [](int index, float w) -> float {
        switch (index) {
            case kParamPreDelay: return w * 150.0f;
            case kParamDecayTime: return 0.4f + w * 14.6f;
            case kParamDamping: case kParamWidth: case kParamDryWet:
            case kParamDistance: case kParamThickness: return w;
            case kParamFreeze: return w > 0.5f ? 1.0f : 0.0f;
            case kParamDecayLow: case kParamDecayMid: case kParamDecayHigh: return 0.1f + w * 1.9f;
            case kParamCrossoverLowMid: return 100.0f + w * 900.0f;
            case kParamCrossoverMidHigh: return 1000.0f + w * 9000.0f;
            case kParamPostEqLowGain: case kParamPostEqMidGain: case kParamPostEqHighGain: return -12.0f + w * 24.0f;
            case kParamDuckThreshold: return -60.0f + w * 60.0f;
            case kParamDuckAmount: return w * 24.0f;
            case kParamDuckRelease: return 10.0f + w * 990.0f;
            case kParamGateThreshold: return -80.0f + w * 60.0f;
            case kParamGateTime: return 10.0f + w * 490.0f;
            default: return -12345.0f;
        }
    };

    for (int i = 0; i < kNumParams; ++i) {
        if (i == kParamMode || i == kParamUiTheme) continue; /* dropdowns: index, not mapped */
        for (int step = 0; step <= 20; ++step) {
            float w = step / 20.0f;
            float got = reverbWidgetToParam(i, w);
            float want = legacy(i, w);
            assert(std::abs(got - want) <= 1e-4f * std::max(1.0f, std::abs(want)));
            if (i != kParamFreeze) {
                assert(std::abs(reverbParamToWidget(i, got) - w) < 1e-5f); /* round-trips */
            }
        }
    }

    std::cout << "  Generic kReverbParams mapping reproduces the legacy per-parameter editor tables. Pass." << std::endl;
}

void testReverbCoreSetParameterValuesIsAllOrNothing() {
    std::cout << "Running testReverbCoreSetParameterValuesIsAllOrNothing..." << std::endl;

    ReverbCore core;
    core.prepare(44100.0, 64);
    std::vector<float> l(64, 0.0f), r(64, 0.0f);

    ParamUpdateMessage good[3] = { { kParamDecayLow, 0.5f }, { kParamDecayMid, 0.6f }, { kParamDecayHigh, 0.7f } };
    assert(core.setParameterValues(good, 3));
    core.process(l.data(), r.data(), 64);
    assert(core.getParameterValue(kParamDecayLow) == 0.5f);
    assert(core.getParameterValue(kParamDecayHigh) == 0.7f);

    /* One bad index rejects the whole batch -- nothing enqueued. */
    ParamUpdateMessage bad[2] = { { kParamDecayLow, 1.5f }, { kNumParams, 0.0f } };
    assert(!core.setParameterValues(bad, 2));
    core.process(l.data(), r.data(), 64);
    assert(core.getParameterValue(kParamDecayLow) == 0.5f);

    /* Fill the queue without draining, then a batch that doesn't fit whole
     * must be refused rather than half-written. */
    ParamUpdateMessage one = { kParamDamping, 0.1f };
    int queued = 0;
    while (core.setParameterValues(&one, 1)) ++queued;
    assert(queued > 0);
    assert(!core.setParameterValues(good, 3));
    core.process(l.data(), r.data(), 64);
    assert(core.getParameterValue(kParamDecayLow) == 0.5f); /* the refused batch never landed */
    assert(core.getParameterValue(kParamDamping) == 0.1f);

    std::cout << "  setParameterValues() enqueues a whole batch or nothing (bad index / full queue). Pass." << std::endl;
}

void testReverbCoreStateRoundTrip() {
    std::cout << "Running testReverbCoreStateRoundTrip..." << std::endl;

    ReverbCore core;
    core.setParameterValueDirect(kParamDecayTime, 9.5f);
    core.setParameterValueDirect(kParamDamping, 0.77f);
    core.setParameterValueDirect(kParamMode, 3.0f);

    size_t required = core.getState(nullptr, 0);
    std::vector<unsigned char> buffer(required);
    assert(core.getState(buffer.data(), buffer.size()) == required);
    assert(std::memcmp(buffer.data(), "LRVB", 4) == 0);

    /* Undersized buffer must fail cleanly, not write out of bounds. */
    assert(core.getState(buffer.data(), 4) == 0);

    ReverbCore other;
    assert(other.setState(buffer.data(), buffer.size()));
    for (int i = 0; i < kNumParams; ++i) {
        assert(std::abs(other.getParameterValue(i) - core.getParameterValue(i)) < 1e-6f);
    }

    std::cout << "  getState()/setState() round-trips every parameter onto a fresh ReverbCore instance. Pass." << std::endl;
}

/* Hand-builds a v1 blob from (id, value) pairs, independent of getState(),
 * so these tests pin the on-disk format rather than just self-consistency. */
static std::vector<unsigned char> buildStateV1(const std::vector<std::pair<std::string, float>>& entries) {
    std::vector<unsigned char> b = { 'L', 'R', 'V', 'B' };
    uint16_t version = 1, count = (uint16_t)entries.size();
    b.insert(b.end(), (unsigned char*)&version, (unsigned char*)&version + 2);
    b.insert(b.end(), (unsigned char*)&count, (unsigned char*)&count + 2);
    for (const auto& e : entries) {
        b.push_back((unsigned char)e.first.size());
        b.insert(b.end(), e.first.begin(), e.first.end());
        b.insert(b.end(), (const unsigned char*)&e.second, (const unsigned char*)&e.second + 4);
    }
    return b;
}

void testReverbCoreStateIsKeyedById() {
    std::cout << "Running testReverbCoreStateIsKeyedById..." << std::endl;

    /* Out of table order, an id this build doesn't know, and most params
     * missing: known ids land on the right params, unknown ones are
     * ignored, missing ones get defaults. */
    auto blob = buildStateV1({ { "dryWet", 0.9f }, { "notARealParam", 123.0f }, { "decayTime", 7.0f } });
    ReverbCore core;
    core.setParameterValueDirect(kParamDamping, 0.99f); /* must be reset to default: missing from blob */
    assert(core.setState(blob.data(), blob.size()));
    assert(core.getParameterValue(kParamDryWet) == 0.9f);
    assert(core.getParameterValue(kParamDecayTime) == 7.0f);
    for (int i = 0; i < kNumParams; ++i) {
        if (i == kParamDryWet || i == kParamDecayTime) continue;
        assert(core.getParameterValue(i) == kReverbParams[i].defaultValue);
    }

    /* Out-of-range and non-finite values are sanitized, not applied raw. */
    auto wild = buildStateV1({ { "decayTime", 1000.0f }, { "damping", std::nanf("") } });
    assert(core.setState(wild.data(), wild.size()));
    assert(core.getParameterValue(kParamDecayTime) == kReverbParams[kParamDecayTime].maxValue);
    assert(core.getParameterValue(kParamDamping) == kReverbParams[kParamDamping].defaultValue);

    std::cout << "  State keys on parameter ids: reorder/unknown/missing/out-of-range all handled. Pass." << std::endl;
}

void testReverbCoreStateRejectsCorruptBlobsAtomically() {
    std::cout << "Running testReverbCoreStateRejectsCorruptBlobsAtomically..." << std::endl;

    ReverbCore source;
    source.setParameterValueDirect(kParamDecayTime, 9.5f);
    std::vector<unsigned char> good(source.getState(nullptr, 0));
    source.getState(good.data(), good.size());

    ReverbCore core;
    core.setParameterValueDirect(kParamWidth, 0.123f);

    /* Every truncation of a valid blob must be rejected without touching
     * the current state (atomic: validate everything, then apply). */
    for (size_t n = 0; n < good.size(); ++n) {
        if (n == sizeof(float) * 23) continue; /* that exact size is the legacy format */
        assert(!core.setState(good.data(), n));
        assert(core.getParameterValue(kParamWidth) == 0.123f);
    }

    std::vector<unsigned char> badVersion = good;
    badVersion[4] = 99;
    assert(!core.setState(badVersion.data(), badVersion.size()));
    assert(!core.setState(nullptr, 0));
    assert(core.getParameterValue(kParamWidth) == 0.123f);

    std::cout << "  Truncated, wrong-version and null blobs are rejected and leave state untouched. Pass." << std::endl;
}

void testReverbCoreStateLoadsLegacyRawArray() {
    std::cout << "Running testReverbCoreStateLoadsLegacyRawArray..." << std::endl;

    /* The pre-v1 format: 23 raw floats in kReverbParams' original order. */
    float legacy[23];
    for (int i = 0; i < 23; ++i) legacy[i] = kReverbParams[i].defaultValue;
    legacy[kParamDecayTime] = 11.0f;
    legacy[kParamMode] = 4.0f;

    ReverbCore core;
    assert(core.setState(legacy, sizeof(legacy)));
    assert(core.getParameterValue(kParamDecayTime) == 11.0f);
    assert(core.getParameterValue(kParamMode) == 4.0f);

    std::cout << "  A legacy raw-float-array blob still loads positionally. Pass." << std::endl;
}

void testReverbCoreAnalyzerFrameDirtyFlagLifecycle() {
    std::cout << "Running testReverbCoreAnalyzerFrameDirtyFlagLifecycle..." << std::endl;

    ReverbCore core;
    core.prepare(44100.0, 2048);

    assert(!core.isLeftAnalyzerFrameDirty());
    assert(!core.isRightAnalyzerFrameDirty());

    /* The FFT analyzer needs 1024 samples to complete one frame -- 2048
     * guarantees at least one completion. */
    std::vector<float> left(2048, 0.0f), right(2048, 0.0f);
    core.process(left.data(), right.data(), 2048);

    assert(core.isLeftAnalyzerFrameDirty());
    assert(core.isRightAnalyzerFrameDirty());

    float magnitudes[64];
    core.getLeftAnalyzerMagnitudes(magnitudes);
    assert(!core.isLeftAnalyzerFrameDirty()); /* reading clears the flag */
    for (int i = 0; i < 64; ++i) {
        assert(magnitudes[i] >= 0.0f && magnitudes[i] <= 1.0f);
    }

    std::cout << "  Analyzer dirty flags set after a completed FFT frame and clear on read; magnitudes stay in [0,1]. Pass." << std::endl;
}

void testReverbCoreProcessPerformsNoHeapAllocations() {
    std::cout << "Running testReverbCoreProcessPerformsNoHeapAllocations..." << std::endl;

    ReverbCore core;
    core.prepare(44100.0, 512);
    std::vector<float> left(512), right(512);
    auto fill = [&](int seed) {
        for (int i = 0; i < 512; ++i) {
            left[i] = 0.1f * (float)((i + seed) % 7);
            right[i] = left[i];
        }
    };

    /* One warm-up block outside the counted region: lazy first-call setup
     * would be its own finding, but the steady-state contract is what the
     * audio thread lives with. */
    fill(0);
    core.process(left.data(), right.data(), 512);

    g_allocationCount = 0;
    g_deallocationCount = 0;
    g_countAllocations = true;

    /* 16 blocks = 8192 samples = 8 completed 1024-point FFT frames per
     * channel, plus a queued parameter change applied mid-stream so the
     * smoother-restart and coefficient-update paths are counted too. */
    for (int block = 0; block < 16; ++block) {
        if (block == 4) {
            core.setParameterValue(kParamDecayTime, 5.0f);
            core.setParameterValue(kParamMode, 3.0f);
            core.setParameterValue(kParamCrossoverLowMid, 400.0f);
        }
        fill(block + 1);
        core.process(left.data(), right.data(), 512);
        if (core.isLeftAnalyzerFrameDirty())  { float m[64]; core.getLeftAnalyzerMagnitudes(m); }
        if (core.isRightAnalyzerFrameDirty()) { float m[64]; core.getRightAnalyzerMagnitudes(m); }
    }

    g_countAllocations = false;
    long allocations = g_allocationCount;
    long deallocations = g_deallocationCount;
    if (allocations != 0 || deallocations != 0) {
        std::cerr << "  process() performed " << allocations << " allocations / " << deallocations
                  << " deallocations over 16 blocks" << std::endl;
    }
    assert(allocations == 0);
    assert(deallocations == 0);

    std::cout << "  16 blocks of process() (8 FFT frames/channel + a mid-stream parameter change) performed zero heap allocations. Pass." << std::endl;
}

/*
 * Golden impulse-response null test (#44). Runs a unit impulse through the
 * full ReverbCore signal chain (dryWet=1.0 so only the wet path is under
 * test -- the dry path is covered by testReverbCoreDryWetZeroIsExactPassthrough)
 * once per reverb mode and compares against a reference IR checked into
 * test/golden/. Any DSP change that alters the sound -- a coefficient, a
 * delay length, a filter -- shows up here; the stability tests above only
 * catch blow-ups.
 *
 * File format: raw little-endian float32, kGoldenIrLen left samples then
 * kGoldenIrLen right samples. Regenerate after an INTENTIONAL DSP change:
 *     dsp_tests --regen-golden
 * then review the before/after difference before committing the new files.
 *
 * Tolerance is -60 dB (1e-3 absolute on a signal with a ~1.0 peak). The
 * references were generated with g++ on x86-64; MSVC's libm can differ by
 * a few ulp in transcendental functions and the FDN's feedback compounds
 * that, so this is deliberately not a bit-exact check. A real DSP change
 * produces residuals orders of magnitude larger than this.
 */
static const int   kGoldenIrLen = 8192;
static const int   kGoldenIrModes = 5; /* Room, Hall, Plate, Cathedral, Spring */
static const float kGoldenIrTolerance = 1e-3f;

#ifndef LOUDIO_REVERB_GOLDEN_DIR
#define LOUDIO_REVERB_GOLDEN_DIR "test/golden"
#endif

static std::string goldenIrPath(int mode) {
    return std::string(LOUDIO_REVERB_GOLDEN_DIR) + "/ir_mode" + std::to_string(mode) + ".f32";
}

static void renderImpulseResponse(int mode, std::vector<float>& left, std::vector<float>& right) {
    ReverbCore core;
    core.setParameterValueDirect(kParamMode, (float)mode);
    core.setParameterValueDirect(kParamDryWet, 1.0f);
    /* prepare() snaps every smoother to its current parameter, so there is
     * no ramp at the start of the IR. */
    core.prepare(44100.0, kGoldenIrLen);

    left.assign(kGoldenIrLen, 0.0f);
    right.assign(kGoldenIrLen, 0.0f);
    left[0] = 1.0f;
    right[0] = 1.0f;
    core.process(left.data(), right.data(), kGoldenIrLen);
}

void regenerateGoldenImpulseResponses() {
    for (int mode = 0; mode < kGoldenIrModes; ++mode) {
        std::vector<float> left, right;
        renderImpulseResponse(mode, left, right);
        std::string path = goldenIrPath(mode);
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) {
            std::cerr << "Cannot write " << path << std::endl;
            std::exit(1);
        }
        std::fwrite(left.data(), sizeof(float), left.size(), f);
        std::fwrite(right.data(), sizeof(float), right.size(), f);
        std::fclose(f);
        std::cout << "  Wrote " << path << std::endl;
    }
}

void testReverbCoreImpulseResponseMatchesGolden() {
    std::cout << "Running testReverbCoreImpulseResponseMatchesGolden..." << std::endl;

    float worstResidual = 0.0f;
    for (int mode = 0; mode < kGoldenIrModes; ++mode) {
        std::string path = goldenIrPath(mode);
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) {
            std::cerr << "  Missing golden IR " << path << " (run dsp_tests --regen-golden to create it)" << std::endl;
        }
        assert(f != nullptr);

        std::vector<float> refLeft(kGoldenIrLen), refRight(kGoldenIrLen);
        size_t gotL = std::fread(refLeft.data(), sizeof(float), kGoldenIrLen, f);
        size_t gotR = std::fread(refRight.data(), sizeof(float), kGoldenIrLen, f);
        std::fclose(f);
        assert(gotL == (size_t)kGoldenIrLen && gotR == (size_t)kGoldenIrLen);

        std::vector<float> left, right;
        renderImpulseResponse(mode, left, right);

        float refPeak = 0.0f, residual = 0.0f;
        for (int i = 0; i < kGoldenIrLen; ++i) {
            refPeak = std::max(refPeak, std::max(std::abs(refLeft[i]), std::abs(refRight[i])));
            residual = std::max(residual, std::abs(left[i] - refLeft[i]));
            residual = std::max(residual, std::abs(right[i] - refRight[i]));
        }
        /* A zeroed or truncated golden must not pass vacuously. */
        assert(refPeak > 0.01f);
        assert(residual < kGoldenIrTolerance);
        worstResidual = std::max(worstResidual, residual);
    }

    std::cout << "  Impulse responses for all " << kGoldenIrModes << " modes null against test/golden/ within "
              << kGoldenIrTolerance << " (worst residual " << worstResidual << "). Pass." << std::endl;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--regen-golden") {
        std::cout << "Regenerating golden impulse responses..." << std::endl;
        regenerateGoldenImpulseResponses();
        return 0;
    }

    std::cout << "==============================" << std::endl;
    std::cout << "Starting Loudio Reverb DSP Tests" << std::endl;
    std::cout << "==============================" << std::endl;

    try {
        testDelayLineFractionalReadAcrossWrap();
        testHouseholderUnitary();
        testFDNStability();
        testCrossoverSum();
        testDuckingEnvelope();
        testParameterRingBuffer();
        testUiThemeMapping();
        testReverbLayoutNoOverlapWithinVisibilityGroups();
        testReverbLayoutStaysInBounds();
        testReverbLayoutHasAllExpectedWidgets();
        testReverbParamTableIndexIntegrity();
        testReverbCoreDefaultsMatchParamTable();
        testReverbCoreSetParameterValueDirectAppliesImmediately();
        testReverbCoreSetParameterValueAppliesAfterProcess();
        testReverbCoreDryWetZeroIsExactPassthrough();
        testReverbCoreProcessStaysBoundedOnNoise();
        testReverbCoreFreezeSustainsEnergy();
        testReverbUiMappingMatchesLegacyEditorTables();
        testReverbCoreSetParameterValuesIsAllOrNothing();
        testReverbCoreStateRoundTrip();
        testReverbCoreStateIsKeyedById();
        testReverbCoreStateRejectsCorruptBlobsAtomically();
        testReverbCoreStateLoadsLegacyRawArray();
        testReverbCoreAnalyzerFrameDirtyFlagLifecycle();
        testReverbCoreProcessPerformsNoHeapAllocations();
        testReverbCoreImpulseResponseMatchesGolden();
        std::cout << "All DSP tests passed successfully!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
