#pragma once
#include <cmath>

/* dB <-> linear helpers shared by every dynamics block (P6, #132). */
namespace loudio {

constexpr float kMinusInfinityDb = -100.0f;

inline float dbToGain(float db) {
    return db <= kMinusInfinityDb ? 0.0f : std::pow(10.0f, db * 0.05f);
}

/* Floored at kMinusInfinityDb so silence never produces -inf/NaN downstream. */
inline float gainToDb(float gain) {
    if (gain <= 0.0f) return kMinusInfinityDb;
    float db = 20.0f * std::log10(gain);
    return db < kMinusInfinityDb ? kMinusInfinityDb : db;
}

} // namespace loudio
