#pragma once

/*
 * JUCE-free replacement for juce::LinearSmoothedValue<float> (#100/B3).
 * Identical semantics to the JUCE class's linear-ramp mode: reset(sampleRate,
 * rampTimeSeconds) sets the ramp length, setTargetValue() starts a new linear
 * ramp from the current value, getNextValue() advances by exactly 1/numSteps
 * per call and returns the new current value.
 */
class SimpleSmoothedValue {
public:
    void reset(double sampleRate, double rampTimeSeconds) {
        stepsToTarget = (int)(rampTimeSeconds * sampleRate);
        if (stepsToTarget < 1) stepsToTarget = 1;
        stepsRemaining = 0;
        increment = 0.0f;
    }

    void setCurrentAndTargetValue(float value) {
        currentValue = value;
        targetValue = value;
        stepsRemaining = 0;
        increment = 0.0f;
    }

    void setTargetValue(float newTarget) {
        if (newTarget == targetValue) return;
        targetValue = newTarget;
        stepsRemaining = stepsToTarget;
        increment = (targetValue - currentValue) / (float)stepsToTarget;
    }

    float getNextValue() {
        if (stepsRemaining <= 0) return currentValue;
        currentValue += increment;
        --stepsRemaining;
        if (stepsRemaining == 0) currentValue = targetValue; /* land exactly on target, no residual float drift */
        return currentValue;
    }

    float getCurrentValue() const { return currentValue; }

private:
    int stepsToTarget = 1;
    int stepsRemaining = 0;
    float currentValue = 0.0f;
    float targetValue = 0.0f;
    float increment = 0.0f;
};
