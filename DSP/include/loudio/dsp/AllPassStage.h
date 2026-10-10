#pragma once
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * First-order all-pass sections for the Phaser (P7, #133).
 *
 * One stage: y[n] = a x[n] + x[n-1] - a y[n-1], a = (tan(pi f/fs) - 1) /
 * (tan(pi f/fs) + 1). Unity magnitude at every frequency; the phase passes
 * -90 degrees at the corner frequency f. N cascaded stages give N notches
 * when mixed with the dry signal, which is what a phaser is.
 */
namespace loudio {

class AllPassStage {
public:
    void clear() { x1 = y1 = 0.0f; }

    void setCornerFrequency(double sampleRate, float hz) {
        float nyquist = (float)sampleRate * 0.49f;
        if (hz < 1.0f) hz = 1.0f;
        if (hz > nyquist) hz = nyquist;
        float t = std::tan((float)M_PI * hz / (float)sampleRate);
        a = (t - 1.0f) / (t + 1.0f);
    }

    float process(float x) {
        float y = a * x + x1 - a * y1;
        x1 = x;
        y1 = y;
        return y;
    }

private:
    float a = 0.0f;
    float x1 = 0.0f, y1 = 0.0f;
};

template <int N>
class AllPassChain {
public:
    static_assert(N > 0, "AllPassChain needs at least one stage");

    void clear() { for (auto& s : stages) s.clear(); }

    /* All stages at one corner (the classic phaser), or spread them. */
    void setCornerFrequency(double sampleRate, float hz) {
        for (auto& s : stages) s.setCornerFrequency(sampleRate, hz);
    }
    void setStageCornerFrequency(int i, double sampleRate, float hz) {
        if (i >= 0 && i < N) stages[i].setCornerFrequency(sampleRate, hz);
    }

    float process(float x) {
        for (auto& s : stages) x = s.process(x);
        return x;
    }

    static constexpr int stageCount() { return N; }

private:
    AllPassStage stages[N];
};

} // namespace loudio
