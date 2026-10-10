#pragma once

class OnePoleLPF {
public:
    OnePoleLPF() = default;
    ~OnePoleLPF() = default;

    void prepare() {
        z1 = 0.0f;
    }

    void clear() {
        z1 = 0.0f;
    }

    float process(float x, float g_hf) {
        /* Output: y = (1 - g_hf)*x + g_hf*y_prev */
        float y = (1.0f - g_hf) * x + g_hf * z1;
        z1 = y;
        return y;
    }

private:
    float z1 = 0.0f;
};
