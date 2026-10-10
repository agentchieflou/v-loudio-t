#pragma once
#include "DelayLine.h"

/*
 * Fixed integer delay for lookahead limiting/compression (P6, #132): the
 * audio is delayed by exactly latencySamples() so the gain computed from
 * the undelayed detector arrives in time. Report latencySamples() through
 * lpi.latency.v1 / setLatencySamples. The delay is fixed per activation
 * (lpi.latency.v1's contract), so change it before prepare()/activate.
 */
namespace loudio {

class Lookahead {
public:
    void prepare(int maxSamples) {
        maxDelay = maxSamples < 0 ? 0 : maxSamples;
        line.prepare(maxDelay + 1);
        applyDelay();
    }

    void clear() { line.clear(); }

    /* May be called before prepare(); the value is clamped to the prepared
     * maximum once prepare() runs. */
    void setDelaySamples(int samples) {
        requested = samples < 0 ? 0 : samples;
        applyDelay();
    }

    int latencySamples() const { return delay; }

    float process(float x) {
        if (delay == 0) return x;
        /* Read before write: read(D) then returns x[n-D] exactly. */
        float out = line.read((float)delay);
        line.write(x);
        return out;
    }

private:
    void applyDelay() { delay = requested > maxDelay ? maxDelay : requested; }

    DelayLine line;
    int requested = 0;
    int delay = 0;
    int maxDelay = 0;
};

} // namespace loudio
