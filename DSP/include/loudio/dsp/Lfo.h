#pragma once
#include <cmath>
#include <cstdint>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * Low-frequency oscillator (P7, #133) for Delay, Flanger, Phaser, Chorus,
 * Tremolo. Output is bipolar in [-1, 1]. Sine, triangle, saw (rising) and
 * sample-and-hold (a new uniform random value each cycle; deterministic
 * xorshift so tests repeat). Free-running in Hz; tempo sync arrives with
 * P9's transport: setFrequencyHz(bpm / 60 / beatsPerCycle) plus
 * resetPhase() on a transport resync is all it needs.
 *
 * Phase is in cycles [0, 1). A second channel can run the same LFO with a
 * fixed offset via valueAt(phase + offset) without a second oscillator.
 */
namespace loudio {

class Lfo {
public:
    enum class Shape { Sine, Triangle, Saw, SampleHold };

    void prepare(double sampleRate) {
        fs = sampleRate;
        setFrequencyHz(hz);
        resetPhase();
    }

    void setShape(Shape s) { shape = s; }

    void setFrequencyHz(float frequency) {
        hz = frequency < 0.0f ? 0.0f : frequency;
        increment = (double)hz / fs;
    }

    void resetPhase(float startPhase = 0.0f) {
        phase = (double)wrap(startPhase);
        heldValue = nextRandom();
    }

    float phaseCycles() const { return (float)phase; }

    /* Returns the value at the current phase, then advances one sample.
     * The phase accumulates in double so a long run doesn't drift. */
    float process() {
        float v = valueAt((float)phase);
        phase += increment;
        if (phase >= 1.0) {
            phase -= 1.0;
            heldValue = nextRandom();
        }
        return v;
    }

    /* Value of the current shape at an arbitrary phase (cycles; any range).
     * SampleHold ignores the phase and returns this cycle's held value. */
    float valueAt(float p) const {
        p = wrap(p);
        switch (shape) {
            case Shape::Sine:     return std::sin(2.0f * (float)M_PI * p);
            case Shape::Triangle: return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;
            case Shape::Saw:      return 2.0f * p - 1.0f;
            case Shape::SampleHold: return heldValue;
        }
        return 0.0f;
    }

private:
    static float wrap(float p) {
        p -= std::floor(p);
        return p < 0.0f ? 0.0f : (p >= 1.0f ? 0.0f : p);
    }

    float nextRandom() {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float)(rng & 0xFFFFFF) / 8388607.5f - 1.0f;
    }

    double fs = 44100.0;
    Shape shape = Shape::Sine;
    float hz = 1.0f;
    double increment = 0.0;
    double phase = 0.0;
    float heldValue = 0.0f;
    uint32_t rng = 0x9E3779B9u;
};

} // namespace loudio
