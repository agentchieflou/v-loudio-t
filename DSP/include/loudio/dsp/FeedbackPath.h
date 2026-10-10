#pragma once
#include <cmath>

/*
 * Feedback gain with a tanh soft clip (P7, #133) so feedback at or above
 * unity can't run away: |output| < 1 for any input, and below ~0.3 the
 * clip is transparent (tanh(x) ~ x). */
namespace loudio {

class FeedbackPath {
public:
    void setGain(float g) { gain = g; }
    float process(float x) const { return std::tanh(gain * x); }

private:
    float gain = 0.0f;
};

} // namespace loudio
