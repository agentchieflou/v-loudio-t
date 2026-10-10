#pragma once
#include <cmath>
#include <vector>

/*
 * JUCE-free replacement for juce::dsp::FFT (#100/B3). A standard iterative
 * radix-2 Cooley-Tukey in-place complex FFT (textbook algorithm, size must
 * be a power of two -- matches the existing FFTAnalyzer's fixed 1024-point
 * size). Not a real-only optimized transform (juce::dsp::FFT's
 * performRealOnlyForwardTransform is) -- this runs a full complex FFT with
 * the input's imaginary part left at zero, which is measurably more compute
 * than a true real-FFT, but at 1024 points, once per UI-refresh-rate-ish
 * cadence (not per audio sample), the cost is not remotely a bottleneck.
 *
 * Operates on the same interleaved [re0, im0, re1, im1, ...] layout
 * FFTAnalyzer.h's fftData already used with juce::dsp::FFT, so the only
 * change needed at the call site is which class performs the transform --
 * downstream magnitude/dB code is completely unaffected.
 */
class SimpleFFT {
public:
    explicit SimpleFFT(int order) : size(1 << order) {}

    int getSize() const { return size; }

    /* In-place forward transform. data must be size*2 floats: data[2*i] =
     * real part of sample i, data[2*i+1] = imaginary part (0 for real
     * input). On return, data[2*k]/data[2*k+1] hold the real/imaginary
     * parts of frequency bin k. */
    void performRealOnlyForwardTransform(float* data) const {
        bitReversalPermute(data);

        for (int len = 2; len <= size; len <<= 1) {
            float angle = -2.0f * (float)M_PI / (float)len;
            float wlenRe = cosf(angle);
            float wlenIm = sinf(angle);

            for (int i = 0; i < size; i += len) {
                float wRe = 1.0f, wIm = 0.0f;
                int half = len / 2;
                for (int j = 0; j < half; ++j) {
                    int idxU = i + j;
                    int idxV = i + j + half;

                    float uRe = data[2 * idxU];
                    float uIm = data[2 * idxU + 1];
                    float vRe = data[2 * idxV] * wRe - data[2 * idxV + 1] * wIm;
                    float vIm = data[2 * idxV] * wIm + data[2 * idxV + 1] * wRe;

                    data[2 * idxU] = uRe + vRe;
                    data[2 * idxU + 1] = uIm + vIm;
                    data[2 * idxV] = uRe - vRe;
                    data[2 * idxV + 1] = uIm - vIm;

                    float nextWRe = wRe * wlenRe - wIm * wlenIm;
                    float nextWIm = wRe * wlenIm + wIm * wlenRe;
                    wRe = nextWRe;
                    wIm = nextWIm;
                }
            }
        }
    }

private:
    void bitReversalPermute(float* data) const {
        for (int i = 1, j = 0; i < size; ++i) {
            int bit = size >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) {
                std::swap(data[2 * i], data[2 * j]);
                std::swap(data[2 * i + 1], data[2 * j + 1]);
            }
        }
    }

    int size;
};
