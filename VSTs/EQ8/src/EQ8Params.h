#pragma once

#include "loudio/plugin/ParamTable.h"

/*
 * EQ-8 parameter table: 8 bands x {type, frequency, gain, Q} + output gain
 * + bypass = 34 parameters (#136). Single source of truth for the APVTS
 * layout, the LPI parameter info, the state blob and the UI.
 *
 * Band type is a stepped parameter: 0 Off, 1 Low-pass, 2 High-pass,
 * 3 Low shelf, 4 High shelf, 5 Bell, 6 Notch. Frequencies are Hz on a
 * linear parameter range (the UI maps them logarithmically). Order
 * matches EQ8ParamIndex exactly; add at the END, never rename an id.
 */
enum EQ8BandType { kEQ8Off = 0, kEQ8LowPass, kEQ8HighPass, kEQ8LowShelf, kEQ8HighShelf, kEQ8Bell, kEQ8Notch, kEQ8NumTypes };

constexpr int kEQ8Bands = 8;
constexpr int kEQ8ParamsPerBand = 4;
enum EQ8BandParam { kEQ8BandType = 0, kEQ8BandFreq, kEQ8BandGain, kEQ8BandQ };

enum EQ8ParamIndex {
    kEQ8OutputGain = kEQ8Bands * kEQ8ParamsPerBand, /* 32 */
    kEQ8Bypass,                                     /* 33 */
    kEQ8NumParams
};

inline constexpr int eq8BandParam(int band, EQ8BandParam which) { return band * kEQ8ParamsPerBand + (int)which; }

#define EQ8_BAND_ROWS(N, HZ) \
    { eq8BandParam(N - 1, kEQ8BandType), "b" #N "Type", "Band " #N " Type",      0.0f,  6.0f,     5.0f,  1.0f,  loudio::kParamStepped }, \
    { eq8BandParam(N - 1, kEQ8BandFreq), "b" #N "Freq", "Band " #N " Frequency", 20.0f, 20000.0f, HZ,    1.0f,  0 }, \
    { eq8BandParam(N - 1, kEQ8BandGain), "b" #N "Gain", "Band " #N " Gain",      -24.0f, 24.0f,   0.0f,  0.1f,  0 }, \
    { eq8BandParam(N - 1, kEQ8BandQ),    "b" #N "Q",    "Band " #N " Q",         0.1f,  30.0f,    0.707f, 0.01f, 0 },

static const loudio::ParamMeta kEQ8Params[kEQ8NumParams] = {
    EQ8_BAND_ROWS(1, 60.0f)
    EQ8_BAND_ROWS(2, 120.0f)
    EQ8_BAND_ROWS(3, 250.0f)
    EQ8_BAND_ROWS(4, 500.0f)
    EQ8_BAND_ROWS(5, 1000.0f)
    EQ8_BAND_ROWS(6, 2000.0f)
    EQ8_BAND_ROWS(7, 4000.0f)
    EQ8_BAND_ROWS(8, 8000.0f)
    { kEQ8OutputGain, "outputGain", "Output Gain", -24.0f, 24.0f, 0.0f, 0.1f, 0 },
    { kEQ8Bypass,     "bypass",     "Bypass",      0.0f,   1.0f,  0.0f, 1.0f, loudio::kParamBoolean },
};
#undef EQ8_BAND_ROWS

static const loudio::ParamTable kEQ8ParamTable = { kEQ8Params, kEQ8NumParams };

[[maybe_unused]] static const char* kEQ8TypeLabels[kEQ8NumTypes] = { "Off", "Low-pass", "High-pass", "Low shelf", "High shelf", "Bell", "Notch" };
