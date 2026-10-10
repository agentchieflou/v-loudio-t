#pragma once
#include "SimpleBiquad.h"

/*
 * Sidechain high-pass (P6, #132): a 2nd-order Butterworth high-pass on the
 * detector path so bass doesn't pump the compressor. Cutoff 0 bypasses.
 */
namespace loudio {

class SidechainHpf {
public:
    void prepare(double sampleRate) {
        fs = sampleRate;
        filter.reset();
        setCutoff(cutoffHz);
    }

    void clear() { filter.reset(); }

    void setCutoff(float hz) {
        cutoffHz = hz < 0.0f ? 0.0f : hz;
        float nyquist = (float)fs * 0.49f;
        if (cutoffHz > nyquist) cutoffHz = nyquist;
        if (cutoffHz > 0.0f) filter.makeHighPass(fs, cutoffHz, 0.70710678f);
    }

    float process(float x) { return cutoffHz > 0.0f ? filter.processSample(x) : x; }

private:
    double fs = 44100.0;
    float cutoffHz = 0.0f;
    SimpleBiquad filter;
};

} // namespace loudio
