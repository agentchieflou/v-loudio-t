/*
 * Unit tests for the shared DSP components in DSP/include/loudio/dsp.
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

#include <cassert>
#include <cmath>
#include <iostream>
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

int main() {
    std::cout << "==============================" << std::endl;
    std::cout << "Starting Loudio DSP component tests" << std::endl;
    std::cout << "==============================" << std::endl;

    testDelayLineFractionalReadAcrossWrap();
    testCrossoverSum();
    testDuckingEnvelope();

    std::cout << "All DSP component tests passed successfully!" << std::endl;
    return 0;
}
