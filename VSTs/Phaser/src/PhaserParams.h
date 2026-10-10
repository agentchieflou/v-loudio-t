#pragma once

#include "loudio/plugin/ParamTable.h"

/*
 * Phaser parameter table (#144). Order matches PhaserParamIndex exactly
 * (asserted by DSPTests.cpp). Add at the END, never rename an id.
 * `stages` is stepped: 0 -> 4, 1 -> 6, 2 -> 8, 3 -> 12 all-pass sections.
 */
enum PhaserParamIndex {
    kPhaserRate = 0,    /* Hz */
    kPhaserDepth,       /* %, 100 = +-2 octaves */
    kPhaserCentre,      /* Hz */
    kPhaserFeedback,    /* %, negative inverts */
    kPhaserStereoPhase, /* degrees */
    kPhaserStages,      /* stepped */
    kPhaserMix,         /* % wet */
    kPhaserNumParams
};

constexpr int kPhaserNumStageOptions = 4;
constexpr int kPhaserStageCounts[kPhaserNumStageOptions] = { 4, 6, 8, 12 };
constexpr int kPhaserMaxStages = 12;
constexpr float kPhaserSweepOctaves = 2.0f;

static const loudio::ParamMeta kPhaserParams[kPhaserNumParams] = {
    { kPhaserRate,        "rate",        "Rate",         0.05f,  10.0f,   0.4f,   0.01f, 0 },
    { kPhaserDepth,       "depth",       "Depth",        0.0f,   100.0f,  70.0f,  1.0f,  0 },
    { kPhaserCentre,      "centre",      "Centre",       100.0f, 5000.0f, 800.0f, 1.0f,  0 },
    { kPhaserFeedback,    "feedback",    "Feedback",     -95.0f, 95.0f,   30.0f,  1.0f,  0 },
    { kPhaserStereoPhase, "stereoPhase", "Stereo Phase", 0.0f,   180.0f,  90.0f,  1.0f,  0 },
    { kPhaserStages,      "stages",      "Stages",       0.0f,   3.0f,    1.0f,   1.0f,  loudio::kParamStepped },
    { kPhaserMix,         "mix",         "Mix",          0.0f,   100.0f,  50.0f,  1.0f,  0 },
};

static const loudio::ParamTable kPhaserParamTable = { kPhaserParams, kPhaserNumParams };

[[maybe_unused]] static const char* kPhaserStageLabels[kPhaserNumStageOptions] = { "4", "6", "8", "12" };
