#pragma once
#include <cmath>

class EnvelopeDetector {
public:
    EnvelopeDetector() = default;
    ~EnvelopeDetector() = default;

    void prepare(double sampleRate) {
        fs = sampleRate;
        envelope = 0.0f;
        setAttackRelease(10.0f, 100.0f); /* default 10ms attack, 100ms release */
    }

    void clear() {
        envelope = 0.0f;
    }

    void setAttackRelease(float attackMs, float releaseMs) {
        if (attackMs < 1.0f) attackMs = 1.0f;
        if (releaseMs < 1.0f) releaseMs = 1.0f;

        g_a = std::exp(-1000.0f / ((float)fs * attackMs));
        g_r = std::exp(-1000.0f / ((float)fs * releaseMs));
    }

    float process(float x) {
        float x_rect = std::abs(x);
        if (x_rect > envelope) {
            envelope = g_a * envelope + (1.0f - g_a) * x_rect;
        } else {
            envelope = g_r * envelope + (1.0f - g_r) * x_rect;
        }
        return envelope;
    }

    float processDb(float x) {
        float env = process(x);
        return 20.0f * std::log10(env + 1e-5f);
    }

private:
    double fs = 44100.0;
    float envelope = 0.0f;
    float g_a = 0.99f;
    float g_r = 0.99f;
};
