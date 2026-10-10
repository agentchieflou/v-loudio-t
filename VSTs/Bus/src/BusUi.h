#pragma once

#include "BusParams.h"
#include "cuif/cuif.h"
#include <cstddef>

/*
 * Bus widget tree (#141), shared by the VST3 editor (VSTs/common) and
 * the LPI offscreen editor (PluginABI/wrapper). JUCE-free; UI thread only.
 *
 * Layout (logical px, 560 x 260): input and output meters on the left;
 * gain, pan and width knobs; a row of toggle buttons for phase L/R,
 * swap, mono and DC block.
 */
typedef void (*BusSendParamsFn)(void* context, const loudio::ParamUpdate* updates, size_t count);

struct BusUiCallbacks {
    void* context;
    BusSendParamsFn sendParams;
};

constexpr int kBusPanelWidth = 560;
constexpr int kBusPanelHeight = 260;
constexpr float kBusMeterY = 20.0f, kBusMeterW = 22.0f, kBusMeterH = 220.0f;
constexpr float kBusInMeterX = 20.0f, kBusOutMeterX = 52.0f;
constexpr float kBusKnobSize = 80.0f, kBusKnobX0 = 110.0f, kBusKnobDx = 90.0f, kBusKnobY = 20.0f;
constexpr float kBusButtonY = 150.0f, kBusButtonW = 76.0f, kBusButtonH = 28.0f, kBusButtonDx = 86.0f;
/* The GUI harness drags the Gain knob (first) at its centre. */
constexpr float kBusGainKnobX = kBusKnobX0, kBusGainKnobY = kBusKnobY;
constexpr float kBusMeterMinDb = -60.0f;

struct BusUi {
    BusUiCallbacks callbacks;
    cuif_widget* root = nullptr;
    cuif_widget* knobs[kBusNumParams] = {};
    cuif_widget* buttons[kBusNumParams] = {};
    cuif_widget* inMeter = nullptr;
    cuif_widget* outMeter = nullptr;
};

BusUi* busUiCreate(float panelWidth, float panelHeight, BusUiCallbacks callbacks);
void busUiDestroy(BusUi* ui);
/* Pushes current parameter values into every widget. Never calls sendParams. */
void busUiSyncFromParams(BusUi* ui, const float* params);
/* Meter readings from the core (dB). UI thread, any rate. */
void busUiFeedMeters(BusUi* ui, float inputDb, float outputDb);
