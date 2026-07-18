#pragma once
#include "SimpleFFT.h"
#include <vector>
#include <cmath>
#include <memory>
#include <algorithm>

class FFTAnalyzer {
public:
    FFTAnalyzer() = default;
    ~FFTAnalyzer() = default;

    void prepare(double sampleRate) {
        (void)sampleRate;
        /* Use 1024 points (10th order FFT) */
        fft = std::make_unique<SimpleFFT>(10);
        fftSize = fft->getSize();

        fifo.assign(fftSize, 0.0f);
        fftData.assign(fftSize * 2, 0.0f);
        fifoIndex = 0;

        /* Prepare Hanning window to prevent spectral leakage */
        window.assign(fftSize, 0.0f);
        for (int i = 0; i < fftSize; ++i) {
            window[i] = 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * i / (float)(fftSize - 1)));
        }
    }

    /* Pushes samples to the analyzer. Returns true if a new FFT frame is calculated! */
    bool pushSample(float sample) {
        fifo[fifoIndex] = sample;
        fifoIndex++;
        if (fifoIndex >= fftSize) {
            fifoIndex = 0;

            /* Copy and apply window */
            std::fill(fftData.begin(), fftData.end(), 0.0f);
            for (int i = 0; i < fftSize; ++i) {
                fftData[i] = fifo[i] * window[i];
            }

            /* Perform complex FFT in-place */
            fft->performRealOnlyForwardTransform(fftData.data());
            return true;
        }
        return false;
    }

    /* Pulls log-spaced magnitudes (64 bins) scaled from 0.0 to 1.0 */
    void getMagnitudes(std::vector<float>& magnitudes) {
        magnitudes.assign(64, 0.0f);

        int numBins = fftSize / 2;

        for (int i = 0; i < 64; ++i) {
            float norm = (float)i / 63.0f;
            /* Logarithmic lookup mapping low bins to higher bins */
            float logIndex = 2.0f * std::pow((float)numBins / 2.0f, norm);
            int idx = (int)logIndex;
            if (idx >= numBins) idx = numBins - 1;
            if (idx < 0) idx = 0;

            float real = fftData[2 * idx];
            float imag = fftData[2 * idx + 1];
            float mag = std::sqrt(real * real + imag * imag);

            /* Convert to dB, normalized log scale [-80dB, 0dB] -> [0.0, 1.0] */
            float db = 20.0f * std::log10(mag + 1e-4f);
            float normalized = (db + 80.0f) / 80.0f;
            if (normalized < 0.0f) normalized = 0.0f;
            if (normalized > 1.0f) normalized = 1.0f;

            magnitudes[i] = normalized;
        }
    }

private:
    std::unique_ptr<SimpleFFT> fft;
    int fftSize = 0;
    std::vector<float> fifo;
    std::vector<float> fftData;
    std::vector<float> window;
    int fifoIndex = 0;
};
