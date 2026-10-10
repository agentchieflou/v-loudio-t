#pragma once

#include "loudio/plugin/ParamTable.h"

/*
 * EQ-3 parameter table (#145): three band gains (-60 dB = kill, +12 dB),
 * two crossover frequencies, three kill switches. Order matches
 * EQ3ParamIndex exactly (asserted by DSPTests.cpp). Add at the END,
 * never rename an id.
 */
enum EQ3ParamIndex {
    kEQ3LowGain = 0,  /* dB */
    kEQ3MidGain,      /* dB */
    kEQ3HighGain,     /* dB */
    kEQ3XoverLowMid,  /* Hz */
    kEQ3XoverMidHigh, /* Hz */
    kEQ3KillLow,      /* bool */
    kEQ3KillMid,      /* bool */
    kEQ3KillHigh,     /* bool */
    kEQ3NumParams
};

constexpr float kEQ3KillDb = -60.0f; /* at or below this a band is silent */

static const loudio::ParamMeta kEQ3Params[kEQ3NumParams] = {
    { kEQ3LowGain,      "lowGain",      "Low",      kEQ3KillDb, 12.0f,    0.0f,    0.1f, 0 },
    { kEQ3MidGain,      "midGain",      "Mid",      kEQ3KillDb, 12.0f,    0.0f,    0.1f, 0 },
    { kEQ3HighGain,     "highGain",     "High",     kEQ3KillDb, 12.0f,    0.0f,    0.1f, 0 },
    { kEQ3XoverLowMid,  "xoverLowMid",  "Low-Mid",  50.0f,      1000.0f,  300.0f,  1.0f, 0 },
    { kEQ3XoverMidHigh, "xoverMidHigh", "Mid-High", 1000.0f,    10000.0f, 3000.0f, 1.0f, 0 },
    { kEQ3KillLow,      "killLow",      "Kill Low",  0.0f,      1.0f,     0.0f,    1.0f, loudio::kParamBoolean },
    { kEQ3KillMid,      "killMid",      "Kill Mid",  0.0f,      1.0f,     0.0f,    1.0f, loudio::kParamBoolean },
    { kEQ3KillHigh,     "killHigh",     "Kill High", 0.0f,      1.0f,     0.0f,    1.0f, loudio::kParamBoolean },
};

static const loudio::ParamTable kEQ3ParamTable = { kEQ3Params, kEQ3NumParams };
