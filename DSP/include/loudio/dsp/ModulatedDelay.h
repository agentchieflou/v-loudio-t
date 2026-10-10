#pragma once
#include "DelayLine.h"
#include "SimpleSmoothedValue.h"
#include "FeedbackPath.h"

/*
 * Delay line with smoothed, modulatable delay time and a soft-clipped
 * feedback path (P7, #133): the core of Delay, Flanger and Chorus.
 *
 *   out = read(delay + modulation)
 *   write(in + feedback(out))
 *
 * The base delay time ramps linearly (SimpleSmoothedValue) so a parameter
 * jump doesn't click; modulation (samples, from an Lfo scaled by depth) is
 * added per sample on top. Fractional reads use DelayLine's exact
 * integer/fraction split (#112). With zero modulation and zero feedback
 * this is a plain fractional delay.
 */
namespace loudio {

class ModulatedDelay {
public:
    void prepare(double sampleRate, float maxDelayMs, float rampSeconds = 0.02f) {
        fs = sampleRate;
        maxDelaySamples = (float)(maxDelayMs * 0.001 * fs);
        line.prepare((int)maxDelaySamples + 4);
        smoothedDelay.reset(fs, rampSeconds);
        smoothedDelay.setCurrentAndTargetValue(clampDelay(targetDelaySamples));
    }

    void clear() { line.clear(); }

    void setDelayMs(float ms) {
        targetDelaySamples = clampDelay((float)(ms * 0.001 * fs));
        smoothedDelay.setTargetValue(targetDelaySamples);
    }

    /* Jump to a delay with no ramp (e.g. on prepare). */
    void setDelayMsImmediate(float ms) {
        targetDelaySamples = clampDelay((float)(ms * 0.001 * fs));
        smoothedDelay.setCurrentAndTargetValue(targetDelaySamples);
    }

    void setFeedback(float gain) { feedback.setGain(gain); }

    float maxDelay() const { return maxDelaySamples; }

    /* modulationSamples is added to the (smoothed) base delay this sample. */
    float process(float in, float modulationSamples = 0.0f) {
        float d = clampDelay(smoothedDelay.getNextValue() + modulationSamples);
        /* Read before write: read(D) then returns x[n-D] (fraction toward x[n-D-1]). */
        float out = line.read(d);
        line.write(in + feedback.process(out));
        return out;
    }

private:
    float clampDelay(float d) const {
        if (d < 1.0f) return 1.0f;
        if (d > maxDelaySamples) return maxDelaySamples;
        return d;
    }

    double fs = 44100.0;
    float maxDelaySamples = 1.0f;
    float targetDelaySamples = 1.0f;
    DelayLine line;
    SimpleSmoothedValue smoothedDelay;
    FeedbackPath feedback;
};

} // namespace loudio
