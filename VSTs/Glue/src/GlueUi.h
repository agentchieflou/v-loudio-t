#pragma once

#include "GlueParams.h"
#include "cuif/cuif.h"
#include <cstddef>

/*
 * Glue widget tree (#140), shared by the VST3 editor (VSTs/common) and
 * the LPI offscreen editor (PluginABI/wrapper). JUCE-free; UI thread only.
 *
 * Layout (logical px, 700 x 300): input / gain-reduction / output meters
 * on the left (the GR meter is the bus compressor's VU); knobs: threshold,
 * makeup, mix, sidechain HPF; switches (dropdowns): ratio, attack,
 * release. Hosts feed the meters with glueUiFeedMeters().
 */
typedef void (*GlueSendParamsFn)(void* context, const loudio::ParamUpdate* updates, size_t count);

struct GlueUiCallbacks {
    void* context;
    GlueSendParamsFn sendParams;
};

constexpr int kGluePanelWidth = 700;
constexpr int kGluePanelHeight = 300;
constexpr float kGlueMeterY = 20.0f, kGlueMeterW = 22.0f, kGlueMeterH = 260.0f;
constexpr float kGlueInMeterX = 20.0f, kGlueGrMeterX = 52.0f, kGlueOutMeterX = 84.0f;
constexpr float kGlueKnobSize = 80.0f, kGlueKnobX0 = 140.0f, kGlueKnobDx = 90.0f, kGlueKnobY = 20.0f;
constexpr float kGlueSwitchY = 150.0f, kGlueSwitchW = 120.0f, kGlueSwitchH = 28.0f, kGlueSwitchDx = 140.0f;
/* The GUI harness drags the Threshold knob (first) at its centre. */
constexpr float kGlueThresholdKnobX = kGlueKnobX0, kGlueThresholdKnobY = kGlueKnobY;
constexpr float kGlueMeterMinDb = -60.0f, kGlueGrMeterRangeDb = 20.0f;

struct GlueUi {
    GlueUiCallbacks callbacks;
    cuif_widget* root = nullptr;
    cuif_widget* knobs[kGlueNumParams] = {};
    cuif_widget* ratioSwitch = nullptr;
    cuif_widget* attackSwitch = nullptr;
    cuif_widget* releaseSwitch = nullptr;
    cuif_widget* inMeter = nullptr;
    cuif_widget* grMeter = nullptr;
    cuif_widget* outMeter = nullptr;
};

GlueUi* glueUiCreate(float panelWidth, float panelHeight, GlueUiCallbacks callbacks);
void glueUiDestroy(GlueUi* ui);
/* Pushes current parameter values into every widget. Never calls sendParams. */
void glueUiSyncFromParams(GlueUi* ui, const float* params);
/* Meter readings from the core (dB; gain reduction >= 0). UI thread, any rate. */
void glueUiFeedMeters(GlueUi* ui, float inputDb, float outputDb, float gainReductionDb);
