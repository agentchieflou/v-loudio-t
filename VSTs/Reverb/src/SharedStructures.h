#pragma once

#include "loudio/plugin/ParamTable.h"

enum ReverbParamIndex {
    kParamPreDelay = 0,
    kParamDecayTime,
    kParamDamping,
    kParamWidth,
    kParamDryWet,
    kParamDistance,
    kParamThickness,
    kParamFreeze,
    kParamMode,
    kParamDecayLow,
    kParamDecayMid,
    kParamDecayHigh,
    kParamCrossoverLowMid,
    kParamCrossoverMidHigh,
    kParamPostEqLowGain,
    kParamPostEqMidGain,
    kParamPostEqHighGain,
    kParamDuckThreshold,
    kParamDuckAmount,
    kParamDuckRelease,
    kParamGateThreshold,
    kParamGateTime,
    kParamUiTheme,
    kNumParams
};

/* The Reverb's parameter metadata and UI->DSP message types are the
 * platform's generic ones (DSP/include/loudio/plugin, #127); these names
 * are kept so the wrappers, editor and tests read unchanged. */
typedef loudio::ParamMeta ReverbParamMeta;
typedef loudio::ParamUpdate ParamUpdateMessage;

/*
 * Single source of truth for every parameter's stable id string, display
 * name, range, default value and flags (#100/B3) -- the VST3 shell's
 * parameters, ReverbCore and the LPI wrapper's
 * lpi_parameter_info table all read from this ONE list, so the numbers
 * can't drift out of sync the way two independently-maintained lists
 * eventually would. Bool/choice parameters (freeze, mode, uiTheme) are
 * represented the same way as every other parameter here -- a plain float
 * range plus a flag -- since at this level (and at LPI's
 * lpi_parameter_info level) they're just a float in [min,max];
 * format-specific display concerns (the VST3 list labels, the toggle's
 * step count) are layered on top by the wrappers, not part of this shared
 * contract.
 *
 * Order matches ReverbParamIndex exactly -- kReverbParams[i].index == i is
 * asserted by DSPTests.cpp.
 */
static const ReverbParamMeta kReverbParams[kNumParams] = {
    { kParamPreDelay,          "preDelay",         "Pre-Delay",           0.0f,   150.0f,  20.0f,   0.1f,  0 },
    { kParamDecayTime,         "decayTime",        "Decay Time",          0.4f,   15.0f,   2.0f,    0.1f,  0 },
    { kParamDamping,           "damping",          "Damping",             0.0f,   1.0f,    0.3f,    0.01f, 0 },
    { kParamWidth,             "width",            "Width",               0.0f,   1.0f,    0.8f,    0.01f, 0 },
    { kParamDryWet,            "dryWet",           "Dry/Wet",             0.0f,   1.0f,    0.4f,    0.01f, 0 },
    { kParamDistance,          "distance",         "Distance",            0.0f,   1.0f,    0.5f,    0.01f, 0 },
    { kParamThickness,         "thickness",        "Thickness",           0.0f,   1.0f,    0.5f,    0.01f, 0 },
    { kParamFreeze,            "freeze",           "Freeze",              0.0f,   1.0f,    0.0f,    1.0f,  loudio::kParamBoolean },
    { kParamMode,              "mode",             "Mode",                0.0f,   4.0f,    1.0f,    1.0f,  loudio::kParamStepped }, /* default "Hall" */
    { kParamDecayLow,          "decayLow",         "Decay Low",           0.1f,   2.0f,    1.0f,    0.01f, 0 },
    { kParamDecayMid,          "decayMid",         "Decay Mid",           0.1f,   2.0f,    1.0f,    0.01f, 0 },
    { kParamDecayHigh,         "decayHigh",        "Decay High",          0.1f,   2.0f,    1.0f,    0.01f, 0 },
    { kParamCrossoverLowMid,   "crossoverLowMid",  "Crossover Low-Mid",   100.0f, 1000.0f, 250.0f,  1.0f,  0 },
    { kParamCrossoverMidHigh,  "crossoverMidHigh", "Crossover Mid-High",  1000.0f,10000.0f,4000.0f, 1.0f,  0 },
    { kParamPostEqLowGain,     "postEqLowGain",    "Post EQ Low Gain",    -12.0f, 12.0f,   0.0f,    0.1f,  0 },
    { kParamPostEqMidGain,     "postEqMidGain",    "Post EQ Mid Gain",    -12.0f, 12.0f,   0.0f,    0.1f,  0 },
    { kParamPostEqHighGain,    "postEqHighGain",   "Post EQ High Gain",   -12.0f, 12.0f,   0.0f,    0.1f,  0 },
    { kParamDuckThreshold,     "duckThreshold",    "Ducking Threshold",   -60.0f, 0.0f,    0.0f,    0.1f,  0 },
    { kParamDuckAmount,        "duckAmount",       "Ducking Amount",      0.0f,   24.0f,   0.0f,    0.1f,  0 },
    { kParamDuckRelease,       "duckRelease",      "Ducking Release",     10.0f,  1000.0f, 200.0f,  1.0f,  0 },
    { kParamGateThreshold,     "gateThreshold",    "Gate Threshold",      -80.0f, -20.0f,  -80.0f,  0.1f,  0 },
    { kParamGateTime,          "gateTime",         "Gate Time",           10.0f,  500.0f,  100.0f,  1.0f,  0 },
    { kParamUiTheme,           "uiTheme",          "UI Theme",            0.0f,   2.0f,    0.0f,    1.0f,  loudio::kParamStepped }, /* default "Default" */
};

/* The table the platform helpers (StateBlob, ParamQueue, mapping) take. */
static const loudio::ParamTable kReverbParamTable = { kReverbParams, kNumParams };
