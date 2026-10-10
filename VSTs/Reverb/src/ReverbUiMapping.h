#pragma once

#include "SharedStructures.h"
#include <cmath>

/*
 * Parameter value <-> cuif widget value mapping, shared by the VST3 editor
 * and the LPI wrapper's offscreen editor (via ReverbUi). Header-only and
 * cuif-free so DSPTests.cpp can check it headlessly.
 *
 * Every continuous parameter is linear over kReverbParams' [min, max] --
 * the single source of truth for ranges, replacing the per-parameter
 * switch tables PluginEditor.cpp used to hardcode. Mode and UI Theme are
 * NOT knobs: their dropdowns report the selected index, which is already
 * the parameter value, so they never go through these functions.
 */
inline float reverbParamToWidget(int index, float value) {
    const ReverbParamMeta& m = kReverbParams[index];
    float w = (value - m.minValue) / (m.maxValue - m.minValue);
    return w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w);
}

inline float reverbWidgetToParam(int index, float widgetValue) {
    const ReverbParamMeta& m = kReverbParams[index];
    if (index == kParamFreeze) return widgetValue > 0.5f ? 1.0f : 0.0f;
    return m.minValue + widgetValue * (m.maxValue - m.minValue);
}
