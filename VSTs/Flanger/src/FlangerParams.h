#pragma once

#include "loudio/plugin/ParamTable.h"

/*
 * Flanger parameter table (#143). Order matches FlangerParamIndex exactly
 * (asserted by DSPTests.cpp). Add at the END, never rename an id.
 */
enum FlangerParamIndex {
    kFlangerRate = 0,    /* Hz */
    kFlangerDepth,       /* % of the centre delay */
    kFlangerCentre,      /* ms */
    kFlangerFeedback,    /* %, negative inverts */
    kFlangerStereoPhase, /* degrees, right channel's LFO offset */
    kFlangerThroughZero, /* bool */
    kFlangerMix,         /* % wet */
    kFlangerNumParams
};

constexpr float kFlangerMaxCentreMs = 10.0f;

static const loudio::ParamMeta kFlangerParams[kFlangerNumParams] = {
    { kFlangerRate,        "rate",        "Rate",         0.05f,  10.0f,  0.3f,  0.01f, 0 },
    { kFlangerDepth,       "depth",       "Depth",        0.0f,   100.0f, 50.0f, 1.0f,  0 },
    { kFlangerCentre,      "centre",      "Centre",       0.1f,   kFlangerMaxCentreMs, 3.0f, 0.01f, 0 },
    { kFlangerFeedback,    "feedback",    "Feedback",     -95.0f, 95.0f,  30.0f, 1.0f,  0 },
    { kFlangerStereoPhase, "stereoPhase", "Stereo Phase", 0.0f,   180.0f, 90.0f, 1.0f,  0 },
    { kFlangerThroughZero, "throughZero", "Through Zero", 0.0f,   1.0f,   0.0f,  1.0f,  loudio::kParamBoolean },
    { kFlangerMix,         "mix",         "Mix",          0.0f,   100.0f, 50.0f, 1.0f,  0 },
};

static const loudio::ParamTable kFlangerParamTable = { kFlangerParams, kFlangerNumParams };
