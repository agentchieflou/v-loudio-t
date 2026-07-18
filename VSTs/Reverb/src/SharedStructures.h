#pragma once

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

struct ParamUpdateMessage {
    int index;
    float value;
};

/*
 * Single source of truth for every parameter's stable id string, display
 * name, range, and default value (#100/B3) -- both the JUCE wrapper's
 * createParameterLayout() and ReverbCore (and, eventually, the LPI wrapper's
 * lpi_parameter_info table) read from this ONE list, so the numbers can't
 * drift out of sync the way two independently-maintained lists eventually
 * would. Bool/choice parameters (freeze, mode, uiTheme) are represented the
 * same way as every other parameter here -- a plain float range -- since at
 * this level (and at LPI's lpi_parameter_info level) they're just a float in
 * [min,max]; JUCE-specific display concerns (AudioParameterChoice's string
 * labels, AudioParameterBool's automation curve) are layered on top only in
 * createParameterLayout() itself, not part of this shared contract.
 *
 * Order matches ReverbParamIndex exactly -- kReverbParams[i].index == i is
 * asserted by DSPTests.cpp.
 */
struct ReverbParamMeta {
    int index;
    const char* id;
    const char* name;
    float minValue;
    float maxValue;
    float defaultValue;
    float stepValue; /* JUCE NormalisableRange step granularity for float-typed params; ignored (but present) for freeze/mode/uiTheme, which use AudioParameterBool/Choice instead */
};

static const ReverbParamMeta kReverbParams[kNumParams] = {
    { kParamPreDelay,          "preDelay",         "Pre-Delay",           0.0f,   150.0f,  20.0f,   0.1f },
    { kParamDecayTime,         "decayTime",        "Decay Time",          0.4f,   15.0f,   2.0f,    0.1f },
    { kParamDamping,           "damping",          "Damping",             0.0f,   1.0f,    0.3f,    0.01f },
    { kParamWidth,             "width",            "Width",               0.0f,   1.0f,    0.8f,    0.01f },
    { kParamDryWet,            "dryWet",           "Dry/Wet",             0.0f,   1.0f,    0.4f,    0.01f },
    { kParamDistance,          "distance",         "Distance",            0.0f,   1.0f,    0.5f,    0.01f },
    { kParamThickness,         "thickness",        "Thickness",           0.0f,   1.0f,    0.5f,    0.01f },
    { kParamFreeze,            "freeze",           "Freeze",              0.0f,   1.0f,    0.0f,    1.0f },
    { kParamMode,              "mode",             "Mode",                0.0f,   4.0f,    1.0f,    1.0f }, /* default "Hall" */
    { kParamDecayLow,          "decayLow",         "Decay Low",           0.1f,   2.0f,    1.0f,    0.01f },
    { kParamDecayMid,          "decayMid",         "Decay Mid",           0.1f,   2.0f,    1.0f,    0.01f },
    { kParamDecayHigh,         "decayHigh",        "Decay High",          0.1f,   2.0f,    1.0f,    0.01f },
    { kParamCrossoverLowMid,   "crossoverLowMid",  "Crossover Low-Mid",   100.0f, 1000.0f, 250.0f,  1.0f },
    { kParamCrossoverMidHigh,  "crossoverMidHigh", "Crossover Mid-High",  1000.0f,10000.0f,4000.0f, 1.0f },
    { kParamPostEqLowGain,     "postEqLowGain",    "Post EQ Low Gain",    -12.0f, 12.0f,   0.0f,    0.1f },
    { kParamPostEqMidGain,     "postEqMidGain",    "Post EQ Mid Gain",    -12.0f, 12.0f,   0.0f,    0.1f },
    { kParamPostEqHighGain,    "postEqHighGain",   "Post EQ High Gain",   -12.0f, 12.0f,   0.0f,    0.1f },
    { kParamDuckThreshold,     "duckThreshold",    "Ducking Threshold",   -60.0f, 0.0f,    0.0f,    0.1f },
    { kParamDuckAmount,        "duckAmount",       "Ducking Amount",      0.0f,   24.0f,   0.0f,    0.1f },
    { kParamDuckRelease,       "duckRelease",      "Ducking Release",     10.0f,  1000.0f, 200.0f,  1.0f },
    { kParamGateThreshold,     "gateThreshold",    "Gate Threshold",      -80.0f, -20.0f,  -80.0f,  0.1f },
    { kParamGateTime,          "gateTime",         "Gate Time",           10.0f,  500.0f,  100.0f,  1.0f },
    { kParamUiTheme,           "uiTheme",          "UI Theme",            0.0f,   2.0f,    0.0f,    1.0f }, /* default "Default" */
};
