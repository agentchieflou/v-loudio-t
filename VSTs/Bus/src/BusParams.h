#pragma once

#include "loudio/plugin/ParamTable.h"

/*
 * Bus (channel utility) parameter table (#141): gain, pan, width, phase
 * invert per channel, L/R swap, mono sum, DC blocker. Every default is
 * neutral, so a fresh Bus is a bit-exact passthrough.
 *
 * Order matches BusParamIndex exactly (asserted by DSPTests.cpp). Add
 * parameters at the END and never rename an id: state blobs key on it.
 */
enum BusParamIndex {
    kBusGain = 0,  /* dB */
    kBusPan,       /* -100 (left) .. 100 (right), constant power, unity at centre */
    kBusWidth,     /* %: 0 mono, 100 as is, 200 double side */
    kBusPhaseL,    /* bool */
    kBusPhaseR,    /* bool */
    kBusSwap,      /* bool, L <-> R */
    kBusMono,      /* bool, (L + R) / 2 to both */
    kBusDcBlock,   /* bool, 5 Hz high-pass */
    kBusNumParams
};

static const loudio::ParamMeta kBusParams[kBusNumParams] = {
    { kBusGain,    "gain",    "Gain",     -60.0f, 12.0f,  0.0f,   0.1f, 0 },
    { kBusPan,     "pan",     "Pan",      -100.0f, 100.0f, 0.0f,  1.0f, 0 },
    { kBusWidth,   "width",   "Width",    0.0f,   200.0f, 100.0f, 1.0f, 0 },
    { kBusPhaseL,  "phaseL",  "Phase L",  0.0f,   1.0f,   0.0f,   1.0f, loudio::kParamBoolean },
    { kBusPhaseR,  "phaseR",  "Phase R",  0.0f,   1.0f,   0.0f,   1.0f, loudio::kParamBoolean },
    { kBusSwap,    "swap",    "Swap L/R", 0.0f,   1.0f,   0.0f,   1.0f, loudio::kParamBoolean },
    { kBusMono,    "mono",    "Mono",     0.0f,   1.0f,   0.0f,   1.0f, loudio::kParamBoolean },
    { kBusDcBlock, "dcBlock", "DC Block", 0.0f,   1.0f,   0.0f,   1.0f, loudio::kParamBoolean },
};

static const loudio::ParamTable kBusParamTable = { kBusParams, kBusNumParams };
