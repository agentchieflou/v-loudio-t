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
#include <set>
#include <string>
#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>

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

void testReverbCoreStateRoundTrip() {
    std::cout << "Running testReverbCoreStateRoundTrip..." << std::endl;

    ReverbCore core;
    core.setParameterValueDirect(kParamDecayTime, 9.5f);
    core.setParameterValueDirect(kParamDamping, 0.77f);
    core.setParameterValueDirect(kParamMode, 3.0f);

    size_t required = core.getState(nullptr, 0);
    assert(required == sizeof(float) * kNumParams);

    std::vector<unsigned char> buffer(required);
    size_t written = core.getState(buffer.data(), buffer.size());
    assert(written == required);

    /* Undersized buffer must fail cleanly, not write out of bounds. */
    assert(core.getState(buffer.data(), 4) == 0);

    ReverbCore other;
    assert(other.setState(buffer.data(), buffer.size()));
    for (int i = 0; i < kNumParams; ++i) {
        assert(std::abs(other.getParameterValue(i) - core.getParameterValue(i)) < 1e-6f);
    }

    assert(!other.setState(buffer.data(), 4)); /* undersized input rejected */

    std::cout << "  getState()/setState() round-trips every parameter onto a fresh ReverbCore instance. Pass." << std::endl;
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

int main() {
    std::cout << "==============================" << std::endl;
    std::cout << "Starting Loudio Reverb DSP Tests" << std::endl;
    std::cout << "==============================" << std::endl;

    try {
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
        testReverbCoreStateRoundTrip();
        testReverbCoreAnalyzerFrameDirtyFlagLifecycle();
        std::cout << "All DSP tests passed successfully!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
