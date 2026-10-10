#pragma once
#include <cmath>

/*
 * JUCE-free biquad replacing juce::dsp::IIR::Filter<float> +
 * juce::dsp::IIR::Coefficients<float>::makeLowShelf/makePeakFilter/
 * makeHighShelf (#100/B3). Coefficient formulas are the standard RBJ Audio
 * EQ Cookbook (Robert Bristow-Johnson) -- the well-established public
 * reference JUCE's own coefficient generation for these filter types
 * closely follows. Direct Form I difference equation.
 */
class SimpleBiquad {
public:
    void makeLowShelf(double sampleRate, float freqHz, float q, float gainDb) {
        float A = powf(10.0f, gainDb / 40.0f);
        float w0 = 2.0f * (float)M_PI * freqHz / (float)sampleRate;
        float cosw0 = cosf(w0);
        float sinw0 = sinf(w0);
        float alpha = sinw0 / (2.0f * q);
        float sqrtA = sqrtf(A);

        float rawB0 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha);
        float rawB1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0);
        float rawB2 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha);
        float rawA0 = (A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha;
        float rawA1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0);
        float rawA2 = (A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha;

        setNormalizedCoefficients(rawB0, rawB1, rawB2, rawA0, rawA1, rawA2);
    }

    void makeHighShelf(double sampleRate, float freqHz, float q, float gainDb) {
        float A = powf(10.0f, gainDb / 40.0f);
        float w0 = 2.0f * (float)M_PI * freqHz / (float)sampleRate;
        float cosw0 = cosf(w0);
        float sinw0 = sinf(w0);
        float alpha = sinw0 / (2.0f * q);
        float sqrtA = sqrtf(A);

        float rawB0 = A * ((A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha);
        float rawB1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0);
        float rawB2 = A * ((A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha);
        float rawA0 = (A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha;
        float rawA1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosw0);
        float rawA2 = (A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha;

        setNormalizedCoefficients(rawB0, rawB1, rawB2, rawA0, rawA1, rawA2);
    }

    void makePeakFilter(double sampleRate, float freqHz, float q, float gainDb) {
        float A = powf(10.0f, gainDb / 40.0f);
        float w0 = 2.0f * (float)M_PI * freqHz / (float)sampleRate;
        float cosw0 = cosf(w0);
        float sinw0 = sinf(w0);
        float alpha = sinw0 / (2.0f * q);

        float rawB0 = 1.0f + alpha * A;
        float rawB1 = -2.0f * cosw0;
        float rawB2 = 1.0f - alpha * A;
        float rawA0 = 1.0f + alpha / A;
        float rawA1 = -2.0f * cosw0;
        float rawA2 = 1.0f - alpha / A;

        setNormalizedCoefficients(rawB0, rawB1, rawB2, rawA0, rawA1, rawA2);
    }

    float processSample(float x) {
        float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }

private:
    void setNormalizedCoefficients(float rawB0, float rawB1, float rawB2, float rawA0, float rawA1, float rawA2) {
        b0 = rawB0 / rawA0;
        b1 = rawB1 / rawA0;
        b2 = rawB2 / rawA0;
        a1 = rawA1 / rawA0;
        a2 = rawA2 / rawA0;
    }

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
};
