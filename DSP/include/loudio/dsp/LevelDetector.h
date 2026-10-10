#pragma once
#include "Decibels.h"
#include <cmath>

/*
 * Level detector for dynamics processors (P6, #132): peak or RMS, with
 * attack, release and hold. Extends the Reverb's EnvelopeDetector (attack/
 * release only, -100 dB floor) without changing it.
 *
 * Time constants are the usual one-pole convention: a step reaches 63.2%
 * (1 - 1/e) of its final value in one attack time, and decays to 36.8% in
 * one release time. Hold: once the input falls below the envelope the
 * envelope is frozen for `hold` ms before release starts; any new peak
 * restarts the hold.
 *
 * RMS mode smooths the squared signal with the same coefficients and
 * returns the square root, so a sine of amplitude 1 reads 0.707 (-3 dB).
 */
namespace loudio {

class LevelDetector {
public:
    enum class Mode { Peak, Rms };

    void prepare(double sampleRate) {
        fs = sampleRate;
        clear();
        setAttackRelease(attackMs, releaseMs);
        setHold(holdMs);
    }

    void clear() {
        state = 0.0f;
        holdCounter = 0;
    }

    void setMode(Mode m) { mode = m; }

    void setAttackRelease(float attack, float release) {
        attackMs = attack < 0.01f ? 0.01f : attack;
        releaseMs = release < 0.01f ? 0.01f : release;
        gA = std::exp(-1000.0f / ((float)fs * attackMs));
        gR = std::exp(-1000.0f / ((float)fs * releaseMs));
    }

    void setHold(float hold) {
        holdMs = hold < 0.0f ? 0.0f : hold;
        holdSamples = (int)(holdMs * 0.001f * (float)fs + 0.5f);
    }

    /* Returns the linear envelope (peak amplitude or RMS). */
    float process(float x) {
        float in = mode == Mode::Peak ? std::abs(x) : x * x;
        if (in > state) {
            state = gA * state + (1.0f - gA) * in;
            holdCounter = holdSamples;
        } else if (holdCounter > 0) {
            --holdCounter;
        } else {
            state = gR * state + (1.0f - gR) * in;
        }
        return mode == Mode::Peak ? state : std::sqrt(state);
    }

    float processDb(float x) { return gainToDb(process(x)); }

    float currentLevel() const { return mode == Mode::Peak ? state : std::sqrt(state); }

private:
    double fs = 44100.0;
    Mode mode = Mode::Peak;
    float attackMs = 10.0f, releaseMs = 100.0f, holdMs = 0.0f;
    float gA = 0.99f, gR = 0.99f;
    int holdSamples = 0;
    float state = 0.0f;
    int holdCounter = 0;
};

} // namespace loudio
