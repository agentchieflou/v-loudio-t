#pragma once
#include "DelayLine.h"
#include <vector>

class SchroederAllPass {
public:
    SchroederAllPass() = default;
    ~SchroederAllPass() = default;

    void prepare(int delayLength, float feedbackGain) {
        delayLine.prepare(delayLength + 1);
        delaySamples = (float)delayLength;
        g = feedbackGain;
    }

    void clear() {
        delayLine.clear();
    }

    float process(float x) {
        float v_delay = delayLine.read(delaySamples);
        float v = x + g * v_delay;
        delayLine.write(v);
        return -g * v + v_delay;
    }

private:
    DelayLine delayLine;
    float delaySamples = 0.0f;
    float g = 0.0f;
};

class AllPassDiffuser {
public:
    AllPassDiffuser() = default;
    ~AllPassDiffuser() = default;

    void prepare(const std::vector<int>& delayLengths, float feedbackGain) {
        stages.resize(delayLengths.size());
        for (size_t i = 0; i < delayLengths.size(); ++i) {
            stages[i].prepare(delayLengths[i], feedbackGain);
        }
    }

    void clear() {
        for (auto& stage : stages) {
            stage.clear();
        }
    }

    float process(float x) {
        float y = x;
        for (auto& stage : stages) {
            y = stage.process(y);
        }
        return y;
    }

private:
    std::vector<SchroederAllPass> stages;
};
