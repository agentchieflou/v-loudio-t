#pragma once

#include "loudio/plugin/ParamTable.h"

/*
 * AutoTune parameter table (#146). Order matches AutoTuneParamIndex
 * exactly (asserted by DSPTests.cpp). Add at the END, never rename an id.
 */
enum AutoTuneParamIndex {
    kAutoTuneKey = 0,      /* stepped: 0 C .. 11 B */
    kAutoTuneScale,        /* stepped: see kAutoTuneScaleMasks */
    kAutoTuneRetuneSpeed,  /* ms, 0 = instant */
    kAutoTuneHumanise,     /* %: how much of the natural deviation to keep */
    kAutoTuneFormant,      /* semitones of formant shift */
    kAutoTuneInputType,    /* stepped: voice range, see kAutoTuneRanges */
    kAutoTuneMix,          /* % wet */
    kAutoTuneReference,    /* Hz, A4 */
    kAutoTuneNumParams
};

constexpr int kAutoTuneNumScales = 8;
/* Pitch-class masks, bit 0 = the key's root. */
constexpr int kAutoTuneScaleMasks[kAutoTuneNumScales] = {
    0xFFF,                                                                       /* Chromatic */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),  /* Major */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),  /* Natural minor */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 11),  /* Harmonic minor */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),  /* Dorian */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9),                         /* Pentatonic major */
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10),                        /* Pentatonic minor */
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 10),             /* Blues */
};

constexpr int kAutoTuneNumInputTypes = 5;
struct AutoTuneRange { float minHz, maxHz; };
constexpr AutoTuneRange kAutoTuneRanges[kAutoTuneNumInputTypes] = {
    { 160.0f, 1500.0f }, /* Soprano */
    { 120.0f, 1000.0f }, /* Alto */
    { 80.0f,  700.0f },  /* Tenor */
    { 60.0f,  450.0f },  /* Bass */
    { 60.0f,  1500.0f }, /* Instrument */
};
constexpr float kAutoTuneAbsoluteMinHz = 60.0f; /* what the detector is sized for */

static const loudio::ParamMeta kAutoTuneParams[kAutoTuneNumParams] = {
    { kAutoTuneKey,         "key",         "Key",          0.0f,   11.0f,  0.0f,   1.0f, loudio::kParamStepped },
    { kAutoTuneScale,       "scale",       "Scale",        0.0f,   7.0f,   0.0f,   1.0f, loudio::kParamStepped },
    { kAutoTuneRetuneSpeed, "retuneSpeed", "Retune Speed", 0.0f,   400.0f, 20.0f,  1.0f, 0 },
    { kAutoTuneHumanise,    "humanise",    "Humanise",     0.0f,   100.0f, 0.0f,   1.0f, 0 },
    { kAutoTuneFormant,     "formant",     "Formant",      -12.0f, 12.0f,  0.0f,   0.1f, 0 },
    { kAutoTuneInputType,   "inputType",   "Input Type",   0.0f,   4.0f,   4.0f,   1.0f, loudio::kParamStepped },
    { kAutoTuneMix,         "mix",         "Mix",          0.0f,   100.0f, 100.0f, 1.0f, 0 },
    { kAutoTuneReference,   "reference",   "Reference",    415.0f, 466.0f, 440.0f, 0.1f, 0 },
};

static const loudio::ParamTable kAutoTuneParamTable = { kAutoTuneParams, kAutoTuneNumParams };

[[maybe_unused]] static const char* kAutoTuneKeyLabels[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
[[maybe_unused]] static const char* kAutoTuneScaleLabels[kAutoTuneNumScales] = { "Chromatic", "Major", "Minor", "Harm. minor", "Dorian", "Pent. major", "Pent. minor", "Blues" };
[[maybe_unused]] static const char* kAutoTuneInputLabels[kAutoTuneNumInputTypes] = { "Soprano", "Alto", "Tenor", "Bass", "Instrument" };
