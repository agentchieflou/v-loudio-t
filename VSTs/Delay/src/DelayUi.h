#pragma once

#include "DelayParams.h"
#include "cuif/cuif.h"
#include <cstddef>

/*
 * Delay widget tree (#142), shared by the VST3 editor (VSTs/common) and
 * the LPI offscreen editor (PluginABI/wrapper). JUCE-free; UI thread only.
 *
 * Layout (logical px, 860 x 320): row 1: time L, time R knobs, SYNC
 * toggle, division L/R dropdowns; row 2: feedback, cross feed, low cut,
 * high cut, mod rate, mod depth, mix knobs; input / output meters on the
 * right. Readouts show the effective times (synced or free) the core
 * reports through delayUiFeedStatus().
 */
typedef void (*DelaySendParamsFn)(void* context, const loudio::ParamUpdate* updates, size_t count);

struct DelayUiCallbacks {
    void* context;
    DelaySendParamsFn sendParams;
};

constexpr int kDelayPanelWidth = 860;
constexpr int kDelayPanelHeight = 320;
constexpr float kDelayKnobSize = 80.0f, kDelayKnobX0 = 20.0f, kDelayKnobDx = 90.0f;
constexpr float kDelayRow1Y = 20.0f, kDelayRow2Y = 160.0f;
constexpr float kDelaySyncX = 200.0f, kDelaySyncY = 46.0f, kDelaySyncW = 80.0f, kDelaySyncH = 28.0f;
constexpr float kDelayDivLX = 300.0f, kDelayDivRX = 410.0f, kDelayDivY = 46.0f, kDelayDivW = 100.0f, kDelayDivH = 28.0f;
constexpr float kDelayMeterY = 20.0f, kDelayMeterW = 22.0f, kDelayMeterH = 280.0f;
constexpr float kDelayInMeterX = 790.0f, kDelayOutMeterX = 822.0f;
/* The GUI harness drags the Time L knob (first) at its centre. */
constexpr float kDelayTimeLKnobX = kDelayKnobX0, kDelayTimeLKnobY = kDelayRow1Y;
constexpr float kDelayMeterMinDb = -60.0f;

struct DelayUi {
    DelayUiCallbacks callbacks;
    cuif_widget* root = nullptr;
    cuif_widget* knobs[kDelayNumParams] = {};
    cuif_widget* syncButton = nullptr;
    cuif_widget* divisionL = nullptr;
    cuif_widget* divisionR = nullptr;
    cuif_widget* inMeter = nullptr;
    cuif_widget* outMeter = nullptr;
};

DelayUi* delayUiCreate(float panelWidth, float panelHeight, DelayUiCallbacks callbacks);
void delayUiDestroy(DelayUi* ui);
/* Pushes current parameter values into every widget. Never calls sendParams. */
void delayUiSyncFromParams(DelayUi* ui, const float* params);
/* Meter readings from the core (dB). UI thread, any rate. */
void delayUiFeedMeters(DelayUi* ui, float inputDb, float outputDb);
