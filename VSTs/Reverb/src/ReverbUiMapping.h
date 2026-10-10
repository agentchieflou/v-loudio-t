#pragma once

#include "SharedStructures.h"

/*
 * Parameter value <-> cuif widget value mapping, shared by the VST3 editor
 * and the LPI wrapper's offscreen editor (via ReverbUi). Header-only and
 * cuif-free so DSPTests.cpp can check it headlessly.
 *
 * Every continuous parameter is linear over kReverbParams' [min, max] --
 * the single source of truth for ranges -- via the platform's generic
 * ParamTable mapping (#127); freeze thresholds through its Boolean flag.
 * Mode and UI Theme are NOT knobs: their dropdowns report the selected
 * index, which is already the parameter value, so they never go through
 * these functions.
 */
inline float reverbParamToWidget(int index, float value) {
    return kReverbParamTable.toWidget(index, value);
}

inline float reverbWidgetToParam(int index, float widgetValue) {
    return kReverbParamTable.fromWidget(index, widgetValue);
}
