#pragma once

#include "EQ3Params.h"
#include "cuif/cuif.h"
#include <cstddef>

/*
 * EQ-3 widget tree (#145), shared by the VST3 editor (VSTs/common) and
 * the LPI offscreen editor (PluginABI/wrapper). JUCE-free; UI thread only.
 *
 * Layout (logical px, 520 x 300): three vertical faders (low, mid, high
 * gain, -60..+12 dB, unity at 5/6 of the travel) with a KILL button under
 * each; low-mid and mid-high crossover knobs on the right.
 */
typedef void (*EQ3SendParamsFn)(void* context, const loudio::ParamUpdate* updates, size_t count);

struct EQ3UiCallbacks {
    void* context;
    EQ3SendParamsFn sendParams;
};

constexpr int kEQ3PanelWidth = 520;
constexpr int kEQ3PanelHeight = 300;
constexpr float kEQ3FaderX0 = 40.0f, kEQ3FaderDx = 100.0f, kEQ3FaderY = 20.0f, kEQ3FaderW = 40.0f, kEQ3FaderH = 180.0f;
constexpr float kEQ3KillY = 220.0f, kEQ3KillW = 60.0f, kEQ3KillH = 28.0f;
constexpr float kEQ3KnobSize = 80.0f, kEQ3KnobX0 = 340.0f, kEQ3KnobDx = 90.0f, kEQ3KnobY = 20.0f;
/* The GUI harness drags the Low-Mid crossover knob (first knob) at its centre. */
constexpr float kEQ3XoverKnobX = kEQ3KnobX0, kEQ3XoverKnobY = kEQ3KnobY;

struct EQ3Ui {
    EQ3UiCallbacks callbacks;
    cuif_widget* root = nullptr;
    cuif_widget* faders[3] = {};
    cuif_widget* kills[3] = {};
    cuif_widget* knobs[kEQ3NumParams] = {};
};

EQ3Ui* eq3UiCreate(float panelWidth, float panelHeight, EQ3UiCallbacks callbacks);
void eq3UiDestroy(EQ3Ui* ui);
/* Pushes current parameter values into every widget. Never calls sendParams. */
void eq3UiSyncFromParams(EQ3Ui* ui, const float* params);
