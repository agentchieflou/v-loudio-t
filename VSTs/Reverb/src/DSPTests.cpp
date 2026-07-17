#include "DelayLine.h"
#include "AllPassDiffuser.h"
#include "EarlyReflections.h"
#include "OnePoleLPF.h"
#include "Crossover.h"
#include "EnvelopeDetector.h"
#include "SharedStructures.h"
#include "cuif/ring_buffer.h"
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
        std::cout << "All DSP tests passed successfully!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
