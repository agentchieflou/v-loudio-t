#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

class DelayLine {
public:
    DelayLine() = default;
    ~DelayLine() = default;

    void prepare(int maxSamples) {
        buffer.assign(maxSamples, 0.0f);
        writeIndex = 0;
    }

    void clear() {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

    void write(float sample) {
        if (buffer.empty()) return;
        buffer[writeIndex] = sample;
        writeIndex = (writeIndex + 1);
        if (writeIndex >= (int)buffer.size()) {
            writeIndex = 0;
        }
    }

    float read(float delayInSamples) const {
        if (buffer.empty()) return 0.0f;
        int size = (int)buffer.size();
        /* Split the delay into integer + fractional parts BEFORE involving
         * the write index, so the ring wrap is exact integer arithmetic.
         * The previous single float subtraction (writeIndex - delay) lost
         * precision on wrap: for a delay a hair above an integer (20 ms at
         * 44.1 kHz is 882.00006 after 20 * 0.001f * 44100), -6e-5f + 6617.0f
         * rounds to exactly 6617.0f, which failed the bounds check and
         * returned 0 -- silently dropping one sample per revolution. Found
         * by the golden-IR null test (#44). Semantics for in-range reads are
         * unchanged: (1-f)*x[n-D] + f*x[n-D-1]. */
        int delayInt = (int)delayInSamples;
        float fraction = delayInSamples - (float)delayInt;
        int idx0 = (writeIndex - delayInt) % size;
        if (idx0 < 0) idx0 += size;
        int idx1 = idx0 - 1;
        if (idx1 < 0) idx1 += size;
        return (1.0f - fraction) * buffer[idx0] + fraction * buffer[idx1];
    }

private:
    std::vector<float> buffer;
    int writeIndex = 0;
};
