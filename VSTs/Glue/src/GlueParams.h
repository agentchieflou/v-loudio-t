#pragma once

#include "loudio/plugin/ParamTable.h"

/*
 * Glue parameter table (#140): the single source of truth for every
 * parameter's id, name, range, default, step and flags.
 *
 * Order matches GlueParamIndex exactly (asserted by DSPTests.cpp). Add
 * parameters at the END and never rename an id: state blobs key on it.
 *
 * Ratio, attack and release are stepped switches like the hardware:
 *   ratio   0: 2:1   1: 4:1   2: 10:1
 *   attack  0: 0.1   1: 0.3   2: 1   3: 3   4: 10   5: 30 ms
 *   release 0: 0.1   1: 0.2   2: 0.4   3: 0.8   4: 1.6 s   5: Auto
 */
enum GlueParamIndex {
    kGlueThreshold = 0, /* dB */
    kGlueRatio,         /* switch, see above */
    kGlueAttack,        /* switch */
    kGlueRelease,       /* switch */
    kGlueMakeup,        /* dB */
    kGlueScHpf,         /* Hz, sidechain high-pass; 0 = off */
    kGlueMix,           /* % wet */
    kGlueNumParams
};

constexpr int kGlueNumRatios = 3, kGlueNumAttacks = 6, kGlueNumReleases = 6;
constexpr int kGlueReleaseAuto = 5;
constexpr float kGlueRatios[kGlueNumRatios] = { 2.0f, 4.0f, 10.0f };
constexpr float kGlueAttacksMs[kGlueNumAttacks] = { 0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f };
constexpr float kGlueReleasesMs[kGlueNumReleases] = { 100.0f, 200.0f, 400.0f, 800.0f, 1600.0f, 0.0f /* auto */ };

static const loudio::ParamMeta kGlueParams[kGlueNumParams] = {
    { kGlueThreshold, "threshold", "Threshold",     -40.0f, 0.0f,   -10.0f, 0.1f, 0 },
    { kGlueRatio,     "ratio",     "Ratio",         0.0f,   2.0f,   1.0f,   1.0f, loudio::kParamStepped },
    { kGlueAttack,    "attack",    "Attack",        0.0f,   5.0f,   4.0f,   1.0f, loudio::kParamStepped },
    { kGlueRelease,   "release",   "Release",       0.0f,   5.0f,   5.0f,   1.0f, loudio::kParamStepped },
    { kGlueMakeup,    "makeup",    "Makeup",        0.0f,   24.0f,  0.0f,   0.1f, 0 },
    { kGlueScHpf,     "scHpf",     "Sidechain HPF", 0.0f,   500.0f, 0.0f,   1.0f, 0 },
    { kGlueMix,       "mix",       "Mix",           0.0f,   100.0f, 100.0f, 1.0f, 0 },
};

static const loudio::ParamTable kGlueParamTable = { kGlueParams, kGlueNumParams };

[[maybe_unused]] static const char* kGlueRatioLabels[kGlueNumRatios] = { "2:1", "4:1", "10:1" };
[[maybe_unused]] static const char* kGlueAttackLabels[kGlueNumAttacks] = { "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms" };
[[maybe_unused]] static const char* kGlueReleaseLabels[kGlueNumReleases] = { "0.1 s", "0.2 s", "0.4 s", "0.8 s", "1.6 s", "Auto" };
