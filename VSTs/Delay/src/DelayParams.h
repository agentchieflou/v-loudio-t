#pragma once

#include "loudio/plugin/ParamTable.h"

/*
 * Delay parameter table (#142). Order matches DelayParamIndex exactly
 * (asserted by DSPTests.cpp). Add at the END, never rename an id.
 *
 * Divisions (stepped) are in quarter notes: index -> beats.
 */
enum DelayParamIndex {
    kDelayTimeL = 0,   /* ms, used when sync is off */
    kDelayTimeR,       /* ms */
    kDelaySync,        /* bool */
    kDelayDivisionL,   /* stepped, see kDelayDivisionBeats */
    kDelayDivisionR,
    kDelayFeedback,    /* % */
    kDelayCrossFeed,   /* %: 0 dual mono, 100 ping-pong */
    kDelayLowCut,      /* Hz, in the feedback path */
    kDelayHighCut,     /* Hz, in the feedback path */
    kDelayModRate,     /* Hz */
    kDelayModDepth,    /* ms */
    kDelayMix,         /* % wet */
    kDelayNumParams
};

constexpr int kDelayNumDivisions = 15;
/* 1/32, 1/16T, 1/16, 1/16., 1/8T, 1/8, 1/8., 1/4T, 1/4, 1/4., 1/2T, 1/2, 1/2., 1/1, 1/1. in quarter notes */
constexpr double kDelayDivisionBeats[kDelayNumDivisions] = {
    0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 4.0 / 3.0, 2.0, 3.0, 4.0, 6.0
};
constexpr float kDelayMaxTimeMs = 2000.0f;
constexpr float kDelayMaxModDepthMs = 10.0f;

static const loudio::ParamMeta kDelayParams[kDelayNumParams] = {
    { kDelayTimeL,     "timeL",     "Time L",     1.0f,    kDelayMaxTimeMs, 375.0f, 0.1f, 0 },
    { kDelayTimeR,     "timeR",     "Time R",     1.0f,    kDelayMaxTimeMs, 375.0f, 0.1f, 0 },
    { kDelaySync,      "sync",      "Sync",       0.0f,    1.0f,     0.0f,   1.0f, loudio::kParamBoolean },
    { kDelayDivisionL, "divisionL", "Division L", 0.0f,    14.0f,    6.0f,   1.0f, loudio::kParamStepped },
    { kDelayDivisionR, "divisionR", "Division R", 0.0f,    14.0f,    5.0f,   1.0f, loudio::kParamStepped },
    { kDelayFeedback,  "feedback",  "Feedback",   0.0f,    110.0f,   40.0f,  1.0f, 0 },
    { kDelayCrossFeed, "crossFeed", "Cross Feed", 0.0f,    100.0f,   0.0f,   1.0f, 0 },
    { kDelayLowCut,    "lowCut",    "Low Cut",    20.0f,   2000.0f,  100.0f, 1.0f, 0 },
    { kDelayHighCut,   "highCut",   "High Cut",   1000.0f, 20000.0f, 8000.0f, 1.0f, 0 },
    { kDelayModRate,   "modRate",   "Mod Rate",   0.05f,   10.0f,    0.5f,   0.01f, 0 },
    { kDelayModDepth,  "modDepth",  "Mod Depth",  0.0f,    kDelayMaxModDepthMs, 0.0f, 0.01f, 0 },
    { kDelayMix,       "mix",       "Mix",        0.0f,    100.0f,   35.0f,  1.0f, 0 },
};

static const loudio::ParamTable kDelayParamTable = { kDelayParams, kDelayNumParams };

[[maybe_unused]] static const char* kDelayDivisionLabels[kDelayNumDivisions] = {
    "1/32", "1/16T", "1/16", "1/16.", "1/8T", "1/8", "1/8.", "1/4T", "1/4", "1/4.", "1/2T", "1/2", "1/2.", "1/1", "1/1."
};
