#pragma once

#include "loudio/plugin/ParamTable.h"

/*
 * Gate parameter table (#138): the single source of truth for every
 * parameter's id, name, range, default, step and flags. The APVTS layout,
 * the LPI parameter info, the state blob and the UI all read from it.
 *
 * Order matches GateParamIndex exactly (asserted by DSPTests.cpp). Add
 * parameters at the END and never rename an id: state blobs key on it.
 */
enum GateParamIndex {
    kGateThreshold = 0, /* dB, open above */
    kGateHysteresis,    /* dB, close below threshold - hysteresis */
    kGateAttack,        /* ms, 63% of the way open */
    kGateHold,          /* ms, open after the level drops below the close threshold */
    kGateRelease,       /* ms, 63% of the way closed */
    kGateRange,         /* dB, the closed floor; -80 is silence */
    kGateScHpf,         /* Hz, sidechain high-pass; 0 = off */
    kGateLookahead,     /* ms, becomes the reported latency */
    kGateNumParams
};

constexpr float kGateMaxLookaheadMs = 10.0f;
constexpr float kGateMinRangeDb = -80.0f;

static const loudio::ParamMeta kGateParams[kGateNumParams] = {
    { kGateThreshold,  "threshold",  "Threshold",     -80.0f, 0.0f,    -40.0f, 0.1f,  0 },
    { kGateHysteresis, "hysteresis", "Hysteresis",    0.0f,   24.0f,   6.0f,   0.1f,  0 },
    { kGateAttack,     "attack",     "Attack",        0.01f,  100.0f,  1.0f,   0.01f, 0 },
    { kGateHold,       "hold",       "Hold",          0.0f,   1000.0f, 50.0f,  1.0f,  0 },
    { kGateRelease,    "release",    "Release",       1.0f,   2000.0f, 100.0f, 1.0f,  0 },
    { kGateRange,      "range",      "Range",         kGateMinRangeDb, 0.0f, kGateMinRangeDb, 0.1f, 0 },
    { kGateScHpf,      "scHpf",      "Sidechain HPF", 0.0f,   500.0f,  0.0f,   1.0f,  0 },
    { kGateLookahead,  "lookahead",  "Lookahead",     0.0f,   kGateMaxLookaheadMs, 0.0f, 0.1f, 0 },
};

static const loudio::ParamTable kGateParamTable = { kGateParams, kGateNumParams };
