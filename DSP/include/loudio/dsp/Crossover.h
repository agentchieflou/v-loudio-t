#pragma once
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class FirstOrderFilter {
public:
    FirstOrderFilter() = default;
    ~FirstOrderFilter() = default;

    void prepare() {
        x1 = y1 = 0.0f;
    }

    void clear() {
        x1 = y1 = 0.0f;
    }

    void setLowPass(float sampleRate, float cutoffFreq) {
        float w0 = std::tan((float)M_PI * cutoffFreq / sampleRate);
        float denom = 1.0f + w0;
        b0 = w0 / denom;
        b1 = w0 / denom;
        a1 = (w0 - 1.0f) / denom;
    }

    void setHighPass(float sampleRate, float cutoffFreq) {
        float w0 = std::tan((float)M_PI * cutoffFreq / sampleRate);
        float denom = 1.0f + w0;
        b0 = 1.0f / denom;
        b1 = -1.0f / denom;
        a1 = (w0 - 1.0f) / denom;
    }

    float process(float x) {
        float y = b0 * x + b1 * x1 - a1 * y1;
        x1 = x;
        y1 = y;
        return y;
    }

private:
    float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f;
    float x1 = 0.0f, y1 = 0.0f;
};

class LinkwitzRiley2 {
public:
    LinkwitzRiley2() = default;
    ~LinkwitzRiley2() = default;

    void prepare() {
        f1.prepare();
        f2.prepare();
    }

    void clear() {
        f1.clear();
        f2.clear();
    }

    void setLowPass(float sampleRate, float cutoffFreq) {
        f1.setLowPass(sampleRate, cutoffFreq);
        f2.setLowPass(sampleRate, cutoffFreq);
    }

    void setHighPass(float sampleRate, float cutoffFreq) {
        f1.setHighPass(sampleRate, cutoffFreq);
        f2.setHighPass(sampleRate, cutoffFreq);
    }

    float process(float x) {
        return f2.process(f1.process(x));
    }

private:
    FirstOrderFilter f1, f2;
};

class ThreeBandCrossover {
public:
    ThreeBandCrossover() = default;
    ~ThreeBandCrossover() = default;

    void prepare() {
        lp_fc2.prepare();
        hp_fc2.prepare();
        lp_fc1_low.prepare();
        hp_fc1_mid.prepare();
        lp_fc1_high.prepare();
        hp_fc1_high.prepare();
    }

    void clear() {
        lp_fc2.clear();
        hp_fc2.clear();
        lp_fc1_low.clear();
        hp_fc1_mid.clear();
        lp_fc1_high.clear();
        hp_fc1_high.clear();
    }

    void setCrossoverFrequencies(float sampleRate, float fc1, float fc2) {
        if (fc1 < 20.0f) fc1 = 20.0f;
        if (fc2 > sampleRate * 0.49f) fc2 = sampleRate * 0.49f;
        if (fc1 >= fc2) fc1 = fc2 * 0.9f;

        lp_fc2.setLowPass(sampleRate, fc2);
        hp_fc2.setHighPass(sampleRate, fc2);

        lp_fc1_low.setLowPass(sampleRate, fc1);
        hp_fc1_mid.setHighPass(sampleRate, fc1);

        lp_fc1_high.setLowPass(sampleRate, fc1);
        hp_fc1_high.setHighPass(sampleRate, fc1);
    }

    void process(float x, float& low, float& mid, float& high) {
        float lp_2 = lp_fc2.process(x);
        float hp_2 = hp_fc2.process(x);

        low = lp_fc1_low.process(lp_2);
        /* Invert the highpass output for 2nd order Linkwitz-Riley flat summing */
        mid = -hp_fc1_mid.process(lp_2);

        float hp_2_lp = lp_fc1_high.process(hp_2);
        float hp_2_hp = hp_fc1_high.process(hp_2);
        /* Apply the lowpass-highpass allpass to match phase of the high band */
        high = -(hp_2_lp - hp_2_hp);
    }

private:
    LinkwitzRiley2 lp_fc2;
    LinkwitzRiley2 hp_fc2;

    LinkwitzRiley2 lp_fc1_low;
    LinkwitzRiley2 hp_fc1_mid;

    LinkwitzRiley2 lp_fc1_high;
    LinkwitzRiley2 hp_fc1_high;
};
