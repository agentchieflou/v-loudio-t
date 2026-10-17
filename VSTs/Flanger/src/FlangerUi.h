#pragma once

#include "FlangerParams.h"
#include "cuif/cuif.h"
#include <cstddef>

/*
 * Flanger widget tree (#143), shared by the VST3 editor (VSTs/common/vst3) and
 * the LPI offscreen editor (PluginABI/wrapper). JUCE-free; UI thread only.
 *
 * Layout (logical px, 580 x 220): rate, depth, centre, feedback, stereo
 * phase and mix knobs in a row; a THROUGH ZERO toggle below.
 */
typedef void (*FlangerSendParamsFn)(void* context, const loudio::ParamUpdate* updates, size_t count);

struct FlangerUiCallbacks {
    void* context;
    FlangerSendParamsFn sendParams;
};

constexpr int kFlangerPanelWidth = 580;
constexpr int kFlangerPanelHeight = 220;
constexpr float kFlangerKnobSize = 80.0f, kFlangerKnobX0 = 20.0f, kFlangerKnobDx = 90.0f, kFlangerKnobY = 20.0f;
constexpr float kFlangerTzX = 20.0f, kFlangerTzY = 150.0f, kFlangerTzW = 140.0f, kFlangerTzH = 28.0f;
/* The GUI harness drags the Rate knob (first) at its centre. */
constexpr float kFlangerRateKnobX = kFlangerKnobX0, kFlangerRateKnobY = kFlangerKnobY;

struct FlangerUi {
    FlangerUiCallbacks callbacks;
    cuif_widget* root = nullptr;
    cuif_widget* knobs[kFlangerNumParams] = {};
    cuif_widget* throughZeroButton = nullptr;
};

FlangerUi* flangerUiCreate(float panelWidth, float panelHeight, FlangerUiCallbacks callbacks);
void flangerUiDestroy(FlangerUi* ui);
/* Pushes current parameter values into every widget. Never calls sendParams. */
void flangerUiSyncFromParams(FlangerUi* ui, const float* params);
