#pragma once
#include <cmath>

/*
 * dB-domain static gain curve for dynamics processors (P6, #132).
 *
 * Compressor: threshold T, ratio R (>= 1; kInfiniteRatio for a limiter),
 * soft knee of width W dB centred on T. Quadratic knee after Giannoulis,
 * Massberg & Reiss, "Digital Dynamic Range Compressor Design" (2012):
 *
 *   2(x-T) < -W : y = x
 *   |2(x-T)| <= W: y = x + (1/R - 1) (x - T + W/2)^2 / (2W)
 *   2(x-T) >  W : y = T + (x - T) / R
 *
 * Expander / gate: same shape mirrored below the threshold, with the gain
 * reduction clamped to `range` dB (the gate's floor; a range of 0 means
 * no gating at all, kInfiniteRange means the gate closes fully):
 *
 *   2(x-T) < -W : y = T + (x - T) R
 *   |2(x-T)| <= W: y = x + (1 - R) (x - T - W/2)^2 / (2W)
 *   2(x-T) >  W : y = x
 *
 * gainReductionDb() returns the attenuation to apply, as a positive
 * number of dB (0 = no reduction). Pure function of the input level; the
 * detector's ballistics belong in LevelDetector.
 */
namespace loudio {

class GainComputer {
public:
    enum class Mode { Compressor, Expander };

    static constexpr float kInfiniteRatio = 1.0e6f;
    static constexpr float kInfiniteRange = 1.0e6f;

    void setMode(Mode m) { mode = m; }
    void setThreshold(float db) { threshold = db; }
    void setRatio(float r) { ratio = r < 1.0f ? 1.0f : r; }
    void setKneeWidth(float db) { knee = db < 0.0f ? 0.0f : db; }
    void setRange(float db) { range = db < 0.0f ? 0.0f : db; } /* expander only */

    float outputDb(float inputDb) const {
        float x = inputDb;
        float over = 2.0f * (x - threshold);
        if (mode == Mode::Compressor) {
            float slope = 1.0f / ratio; /* 0 for a limiter */
            if (over < -knee) return x;
            if (over > knee || knee <= 0.0f) return threshold + (x - threshold) * slope;
            float d = x - threshold + knee * 0.5f;
            return x + (slope - 1.0f) * d * d / (2.0f * knee);
        } else {
            float y;
            if (over > knee) {
                y = x;
            } else if (over < -knee || knee <= 0.0f) {
                y = threshold + (x - threshold) * ratio;
            } else {
                float d = x - threshold - knee * 0.5f;
                y = x + (1.0f - ratio) * d * d / (2.0f * knee);
            }
            if (x - y > range) y = x - range;
            return y;
        }
    }

    float gainReductionDb(float inputDb) const {
        float gr = inputDb - outputDb(inputDb);
        return gr < 0.0f ? 0.0f : gr;
    }

private:
    Mode mode = Mode::Compressor;
    float threshold = 0.0f;
    float ratio = 1.0f;
    float knee = 0.0f;
    float range = kInfiniteRange;
};

} // namespace loudio
