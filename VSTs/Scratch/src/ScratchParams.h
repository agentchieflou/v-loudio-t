#pragma once

#include "loudio/plugin/ParamTable.h"

/*
 * Scratch's parameter table: the single source of truth for every
 * parameter's id, name, range, default, step and flags. The APVTS layout,
 * the LPI parameter info, the state blob and the UI mapping all read from
 * it (DSP/include/loudio/plugin/ParamTable.h).
 *
 * Order matches ScratchParamIndex exactly (asserted by DSPTests.cpp). Add
 * parameters at the END and never rename an id: state blobs key on it.
 */
enum ScratchParamIndex {
    kScratchGain = 0,
    kScratchNumParams
};

static const loudio::ParamMeta kScratchParams[kScratchNumParams] = {
    { kScratchGain, "gain", "Gain", 0.0f, 2.0f, 1.0f, 0.01f, 0 },
};

static const loudio::ParamTable kScratchParamTable = { kScratchParams, kScratchNumParams };
