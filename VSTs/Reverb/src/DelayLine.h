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
        float readPos = (float)writeIndex - delayInSamples;
        if (readPos < 0.0f) {
            readPos += (float)size;
            if (readPos < 0.0f) {
                readPos = std::fmod(readPos, (float)size) + (float)size;
            }
        }

        int idx0 = (int)readPos;
        int idx1 = idx0 + 1;
        if (idx1 >= size) idx1 -= size;

        float fraction = readPos - (float)idx0;

        if (idx0 < 0 || idx0 >= size || idx1 < 0 || idx1 >= size) {
            return 0.0f;
        }

        return (1.0f - fraction) * buffer[idx0] + fraction * buffer[idx1];
    }

private:
    std::vector<float> buffer;
    int writeIndex = 0;
};
