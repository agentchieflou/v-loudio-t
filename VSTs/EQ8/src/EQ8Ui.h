#pragma once

#include "EQ8Params.h"
#include "cuif/cuif.h"
#include <cstddef>

/*
 * EQ-8 widget tree, shared by the VST3 editor (VSTs/common/vst3) and the LPI
 * offscreen editor (PluginABI/wrapper). JUCE-free; UI thread only.
 *
 * Layout (logical px, 900 x 520):
 *   - analyzer (post-EQ spectrum) under a transparent EQ curve with one
 *     draggable handle per active band: drag = frequency + gain, wheel = Q;
 *   - a row of 8 band-type dropdowns under the curve;
 *   - output gain knob and bypass button on the right.
 * The curve is recomputed from the UI's own parameter copy
 * (EQ8Response.h) whenever a parameter changes; the editor never touches
 * the audio thread's filters.
 */
typedef void (*EQ8SendParamsFn)(void* context, const loudio::ParamUpdate* updates, size_t count);

struct EQ8UiCallbacks {
    void* context;
    EQ8SendParamsFn sendParams;
};

constexpr int kEQ8PanelWidth = 900;
constexpr int kEQ8PanelHeight = 520;
constexpr float kEQ8CurveX = 20.0f, kEQ8CurveY = 20.0f, kEQ8CurveW = 700.0f, kEQ8CurveH = 340.0f;
constexpr float kEQ8GainKnobX = 760.0f, kEQ8GainKnobY = 60.0f, kEQ8GainKnobSize = 110.0f;
constexpr float kEQ8BypassX = 760.0f, kEQ8BypassY = 200.0f, kEQ8BypassW = 110.0f, kEQ8BypassH = 30.0f;
constexpr int kEQ8ResponsePoints = 256;
constexpr float kEQ8MinHz = 20.0f, kEQ8MaxHz = 20000.0f, kEQ8MinDb = -24.0f, kEQ8MaxDb = 24.0f;

struct EQ8Ui {
    EQ8UiCallbacks callbacks;
    double sampleRate = 48000.0; /* for the response curve; hosts may set it */

    cuif_widget* root = nullptr;
    cuif_widget* analyzer = nullptr;
    cuif_widget* curve = nullptr;
    cuif_widget* typeDropdowns[kEQ8Bands] = {};
    cuif_widget* gainKnob = nullptr;
    cuif_widget* bypassButton = nullptr;

    float params[kEQ8NumParams] = {};    /* the UI's copy, for the response curve */
    float response[kEQ8ResponsePoints] = {};
    float spectrum[64] = {};            /* hosts copy fresh analyzer magnitudes in */
};

EQ8Ui* eq8UiCreate(float panelWidth, float panelHeight, EQ8UiCallbacks callbacks);
void eq8UiDestroy(EQ8Ui* ui);
/* Pushes current parameter values into every widget and recomputes the curve. Never calls sendParams. */
void eq8UiSyncFromParams(EQ8Ui* ui, const float* params);
