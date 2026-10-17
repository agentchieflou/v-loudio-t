#pragma once

#include "GateParams.h"
#include "cuif/cuif.h"
#include <cstddef>

/*
 * Gate widget tree (#138), shared by the VST3 editor (VSTs/common/vst3) and
 * the LPI offscreen editor (PluginABI/wrapper). JUCE-free; UI thread only.
 *
 * Layout (logical px, 600 x 260): input and gain-reduction meters and an
 * OPEN/CLOSED lamp on the left; knobs in two rows: threshold, hysteresis,
 * attack, hold / release, range, sidechain HPF, lookahead.
 * The UI never writes parameters itself: every edit goes out through
 * sendParams. Hosts feed the meters with gateUiFeedMeters() from the
 * core's per-block readings.
 */
typedef void (*GateSendParamsFn)(void* context, const loudio::ParamUpdate* updates, size_t count);

struct GateUiCallbacks {
    void* context;
    GateSendParamsFn sendParams;
};

constexpr int kGatePanelWidth = 600;
constexpr int kGatePanelHeight = 260;
constexpr float kGateMeterY = 20.0f, kGateMeterW = 22.0f, kGateMeterH = 220.0f;
constexpr float kGateInMeterX = 20.0f, kGateGrMeterX = 52.0f;
constexpr float kGateLampX = 90.0f, kGateLampY = 20.0f, kGateLampW = 90.0f, kGateLampH = 28.0f;
constexpr float kGateKnobSize = 80.0f, kGateKnobX0 = 200.0f, kGateKnobDx = 90.0f;
constexpr float kGateKnobRow1Y = 20.0f, kGateKnobRow2Y = 130.0f;
/* The GUI harness drags the Threshold knob (row 1, column 1) at its centre. */
constexpr float kGateThresholdKnobX = kGateKnobX0, kGateThresholdKnobY = kGateKnobRow1Y;
constexpr float kGateMeterMinDb = -80.0f, kGateGrMeterRangeDb = 60.0f;

struct GateUi {
    GateUiCallbacks callbacks;
    cuif_widget* root = nullptr;
    cuif_widget* knobs[kGateNumParams] = {};
    cuif_widget* inMeter = nullptr;
    cuif_widget* grMeter = nullptr;
    cuif_widget* lamp = nullptr; /* a mouse-disabled toggle button: lit = open */
};

GateUi* gateUiCreate(float panelWidth, float panelHeight, GateUiCallbacks callbacks);
void gateUiDestroy(GateUi* ui);
/* Pushes current parameter values into every widget. Never calls sendParams. */
void gateUiSyncFromParams(GateUi* ui, const float* params);
/* Meter readings from the core (dB; gain reduction >= 0). UI thread, any rate. */
void gateUiFeedMeters(GateUi* ui, float inputDb, float gainReductionDb, bool open);
