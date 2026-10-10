#pragma once

#include "loudio/plugin/ParamTable.h"

/*
 * Limiter parameter table (#139): the single source of truth for every
 * parameter's id, name, range, default, step and flags. The APVTS layout,
 * the LPI parameter info, the state blob and the UI all read from it.
 *
 * Order matches LimiterParamIndex exactly (asserted by DSPTests.cpp). Add
 * parameters at the END and never rename an id: state blobs key on it.
 *
 * Oversampling is stepped: 0 Off (sample peaks), 1 2x, 2 4x (true-peak
 * detection on the upsampled signal). Input gain drives the signal into
 * the ceiling; there is deliberately no gain AFTER the ceiling, which
 * would void the brickwall guarantee.
 */
enum LimiterOversampling { kLimiterOsOff = 0, kLimiterOs2x, kLimiterOs4x, kLimiterNumOs };

enum LimiterParamIndex {
    kLimiterCeiling = 0,   /* dBFS, no output sample exceeds it */
    kLimiterRelease,       /* ms, 63% of the way back to unity */
    kLimiterLookahead,     /* ms, becomes (part of) the reported latency */
    kLimiterOversampling,  /* LimiterOversampling */
    kLimiterLink,          /* % stereo link: 0 independent, 100 common gain */
    kLimiterInputGain,     /* dB, before detection */
    kLimiterNumParams
};

constexpr float kLimiterMaxLookaheadMs = 10.0f;

static const loudio::ParamMeta kLimiterParams[kLimiterNumParams] = {
    { kLimiterCeiling,      "ceiling",      "Ceiling",      -30.0f, 0.0f,   -0.3f,  0.1f, 0 },
    { kLimiterRelease,      "release",      "Release",      1.0f,   1000.0f, 100.0f, 1.0f, 0 },
    { kLimiterLookahead,    "lookahead",    "Lookahead",    0.0f,   kLimiterMaxLookaheadMs, 2.0f, 0.1f, 0 },
    { kLimiterOversampling, "oversampling", "Oversampling", 0.0f,   2.0f,   0.0f,   1.0f, loudio::kParamStepped },
    { kLimiterLink,         "link",         "Link",         0.0f,   100.0f, 100.0f, 1.0f, 0 },
    { kLimiterInputGain,    "inputGain",    "Input Gain",   -12.0f, 24.0f,  0.0f,   0.1f, 0 },
};

static const loudio::ParamTable kLimiterParamTable = { kLimiterParams, kLimiterNumParams };

[[maybe_unused]] static const char* kLimiterOsLabels[kLimiterNumOs] = { "Off", "2x", "4x" };
