#pragma once

#include "loudio/plugin/ParamTable.h"

/*
 * Compressor parameter table (#137): the single source of truth for every
 * parameter's id, name, range, default, step and flags. The APVTS layout,
 * the LPI parameter info, the state blob and the UI all read from it.
 *
 * Order matches CompressorParamIndex exactly (asserted by DSPTests.cpp).
 * Add parameters at the END and never rename an id: state blobs key on it.
 *
 * Detector mode is stepped: 0 Peak (|x|, no ballistics of its own; the
 * attack/release act on the gain), 1 RMS (20 ms window; a sine reads 3 dB
 * lower than in Peak mode). Auto makeup, when on, replaces the Makeup
 * knob with the gain reduction a 0 dBFS signal would get, so unity-level
 * material comes out at unity.
 */
enum CompressorDetector { kCompressorDetectorPeak = 0, kCompressorDetectorRms, kCompressorNumDetectors };

enum CompressorParamIndex {
    kCompressorThreshold = 0, /* dB */
    kCompressorRatio,         /* :1 */
    kCompressorKnee,          /* dB, full width centred on the threshold */
    kCompressorAttack,        /* ms, 63% of a step */
    kCompressorRelease,       /* ms, 63% of a step */
    kCompressorLookahead,     /* ms, becomes the reported latency */
    kCompressorMakeup,        /* dB, ignored while auto makeup is on */
    kCompressorMix,           /* % wet */
    kCompressorDetector,      /* CompressorDetector */
    kCompressorAutoMakeup,    /* bool */
    kCompressorScHpf,         /* Hz, sidechain high-pass; 0 = off */
    kCompressorNumParams
};

constexpr float kCompressorMaxLookaheadMs = 10.0f;

static const loudio::ParamMeta kCompressorParams[kCompressorNumParams] = {
    { kCompressorThreshold,  "threshold",  "Threshold",     -60.0f, 0.0f,    -20.0f, 0.1f,  0 },
    { kCompressorRatio,      "ratio",      "Ratio",         1.0f,   20.0f,   4.0f,   0.1f,  0 },
    { kCompressorKnee,       "knee",       "Knee",          0.0f,   24.0f,   6.0f,   0.1f,  0 },
    { kCompressorAttack,     "attack",     "Attack",        0.1f,   200.0f,  10.0f,  0.1f,  0 },
    { kCompressorRelease,    "release",    "Release",       10.0f,  2000.0f, 100.0f, 1.0f,  0 },
    { kCompressorLookahead,  "lookahead",  "Lookahead",     0.0f,   kCompressorMaxLookaheadMs, 0.0f, 0.1f, 0 },
    { kCompressorMakeup,     "makeup",     "Makeup",        0.0f,   24.0f,   0.0f,   0.1f,  0 },
    { kCompressorMix,        "mix",        "Mix",           0.0f,   100.0f,  100.0f, 1.0f,  0 },
    { kCompressorDetector,   "detector",   "Detector",      0.0f,   1.0f,    0.0f,   1.0f,  loudio::kParamStepped },
    { kCompressorAutoMakeup, "autoMakeup", "Auto Makeup",   0.0f,   1.0f,    0.0f,   1.0f,  loudio::kParamBoolean },
    { kCompressorScHpf,      "scHpf",      "Sidechain HPF", 0.0f,   500.0f,  0.0f,   1.0f,  0 },
};

static const loudio::ParamTable kCompressorParamTable = { kCompressorParams, kCompressorNumParams };

[[maybe_unused]] static const char* kCompressorDetectorLabels[kCompressorNumDetectors] = { "Peak", "RMS" };
