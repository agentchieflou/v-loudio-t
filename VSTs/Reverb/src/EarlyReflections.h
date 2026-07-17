#pragma once
#include "DelayLine.h"
#include <vector>

struct ReverbTap {
    int delaySamples;
    float gain;
};

class EarlyReflections {
public:
    EarlyReflections() = default;
    ~EarlyReflections() = default;

    void prepare(int maxDelaySamples, const std::vector<ReverbTap>& taps) {
        delayLine.prepare(maxDelaySamples + 1);
        activeTaps = taps;
    }

    void clear() {
        delayLine.clear();
    }

    float process(float x) {
        delayLine.write(x);
        float y = 0.0f;
        for (const auto& tap : activeTaps) {
            y += tap.gain * delayLine.read((float)tap.delaySamples);
        }
        return y;
    }

private:
    DelayLine delayLine;
    std::vector<ReverbTap> activeTaps;
};
