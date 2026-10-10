/*
 * Scratch DSP tests: the platform's generic core checks
 * (DSP/include/loudio/testing/CoreTests.h) plus this plugin's own.
 *
 *   dsp_tests                 run everything
 *   dsp_tests --regen-golden  rewrite test/golden/ after an INTENTIONAL DSP change
 */
#include "ScratchCore.h"
#include "loudio/testing/CoreTests.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

LOUDIO_DEFINE_ALLOCATION_HOOK();

#ifndef LOUDIO_SCRATCH_GOLDEN_DIR
#define LOUDIO_SCRATCH_GOLDEN_DIR "test/golden"
#endif

namespace {

const char kStateMagic[4] = { 'S', 'C', 'R', 'A' };
const int kGoldenIrLength = 8192;
const double kGoldenSampleRate = 44100.0;

/* One golden IR per variant: add one per mode/preset worth pinning. */
const loudio::testing::GoldenVariant<ScratchCore> kGoldenVariants[] = {
    { "default", nullptr },
};
const int kGoldenVariantCount = (int)(sizeof(kGoldenVariants) / sizeof(kGoldenVariants[0]));

/* Plugin-specific: the scaffold core is a pass-through times gain. Replace
 * with real DSP assertions as the plugin grows. */
void testScratchGainScalesInput() {
    std::cout << "Running testScratchGainScalesInput..." << std::endl;
    ScratchCore core;
    core.prepare(44100.0, 64);
    core.setParameterValueDirect(kScratchGain, 0.5f);
    float l[64], r[64];
    for (int i = 0; i < 64; ++i) { l[i] = 1.0f; r[i] = -1.0f; }
    core.process(l, r, 64);
    for (int i = 0; i < 64; ++i) { assert(std::abs(l[i] - 0.5f) < 1e-6f); assert(std::abs(r[i] + 0.5f) < 1e-6f); }
    std::cout << "  gain = 0.5 halves both channels. Pass." << std::endl;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--regen-golden") {
        std::cout << "Regenerating golden impulse responses..." << std::endl;
        loudio::testing::regenerateGoldenImpulseResponses<ScratchCore>(LOUDIO_SCRATCH_GOLDEN_DIR, kGoldenVariants, kGoldenVariantCount, kGoldenIrLength, kGoldenSampleRate);
        return 0;
    }

    std::cout << "==============================" << std::endl;
    std::cout << "Starting Loudio Scratch DSP Tests" << std::endl;
    std::cout << "==============================" << std::endl;

    using namespace loudio::testing;
    testParamTableIndexIntegrity<ScratchCore>();
    testDefaultsMatchTable<ScratchCore>();
    testDirectSetAppliesImmediately<ScratchCore>(kScratchGain, 0.25f);
    testQueuedSetAppliesAfterProcess<ScratchCore>(kScratchGain, 0.25f);
    testBatchIsAllOrNothing<ScratchCore>(kScratchGain, 0.25f);
    testStateRoundTrip<ScratchCore>(kStateMagic);
    testStateRejectsCorruptBlobs<ScratchCore>();
    testProcessPerformsNoHeapAllocations<ScratchCore>(44100.0, 512, 16, [](ScratchCore& core) {
        core.setParameterValue(kScratchGain, 1.5f);
    });
    testScratchGainScalesInput();
    testImpulseResponseMatchesGolden<ScratchCore>(LOUDIO_SCRATCH_GOLDEN_DIR, kGoldenVariants, kGoldenVariantCount, kGoldenIrLength, kGoldenSampleRate);

    std::cout << "All Scratch DSP tests passed successfully!" << std::endl;
    return 0;
}
