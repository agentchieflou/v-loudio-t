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
