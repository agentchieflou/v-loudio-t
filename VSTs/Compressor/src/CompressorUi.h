#pragma once

#include "CompressorParams.h"
#include "cuif/cuif.h"
#include <cstddef>

/*
 * Compressor widget tree (#137), shared by the VST3 editor (VSTs/common/vst3)
 * and the LPI offscreen editor (PluginABI/wrapper). JUCE-free; UI thread
 * only.
 *
 * Layout (logical px, 920 x 420):
 *   - transfer curve (in -> out dB) with the live operating point, left;
 *   - input / gain-reduction / output meters beside it;
 *   - knobs: threshold, ratio, knee, attack, release (row 1); lookahead,
 *     makeup, mix, sidechain HPF (row 2); detector dropdown and auto
 *     makeup toggle (row 3).
 * The UI never writes parameters itself: every edit goes out through
 * sendParams and each host keeps its own RT-safe path to the core. Hosts
 * feed the meters with compressorUiFeedMeters() from the core's per-block
 * readings.
 */
typedef void (*CompressorSendParamsFn)(void* context, const loudio::ParamUpdate* updates, size_t count);

struct CompressorUiCallbacks {
    void* context;
    CompressorSendParamsFn sendParams;
};

constexpr int kCompressorPanelWidth = 920;
constexpr int kCompressorPanelHeight = 420;
constexpr float kCompressorCurveX = 20.0f, kCompressorCurveY = 20.0f, kCompressorCurveSize = 300.0f;
constexpr float kCompressorMeterY = 20.0f, kCompressorMeterW = 22.0f, kCompressorMeterH = 300.0f;
constexpr float kCompressorInMeterX = 340.0f, kCompressorGrMeterX = 372.0f, kCompressorOutMeterX = 404.0f;
constexpr float kCompressorKnobSize = 80.0f, kCompressorKnobX0 = 460.0f, kCompressorKnobDx = 90.0f;
constexpr float kCompressorKnobRow1Y = 20.0f, kCompressorKnobRow2Y = 130.0f;
constexpr float kCompressorDetectorX = 460.0f, kCompressorDetectorY = 250.0f, kCompressorDetectorW = 120.0f, kCompressorDetectorH = 28.0f;
constexpr float kCompressorAutoX = 600.0f, kCompressorAutoY = 250.0f, kCompressorAutoW = 130.0f, kCompressorAutoH = 28.0f;
/* The GUI harness drags the Makeup knob at its centre (row 2, column 2). */
constexpr float kCompressorMakeupKnobX = kCompressorKnobX0 + kCompressorKnobDx, kCompressorMakeupKnobY = kCompressorKnobRow2Y;
constexpr float kCompressorCurveMinDb = -60.0f, kCompressorCurveMaxDb = 0.0f;
constexpr float kCompressorGrMeterRangeDb = 30.0f;

struct CompressorUi {
    CompressorUiCallbacks callbacks;
    cuif_widget* root = nullptr;
    cuif_widget* knobs[kCompressorNumParams] = {};
    cuif_widget* detectorDropdown = nullptr;
    cuif_widget* autoMakeupButton = nullptr;
    cuif_widget* transfer = nullptr;
    cuif_widget* inMeter = nullptr;
    cuif_widget* grMeter = nullptr;
    cuif_widget* outMeter = nullptr;
    float params[kCompressorNumParams] = {}; /* the UI's copy, for the transfer curve */
};

CompressorUi* compressorUiCreate(float panelWidth, float panelHeight, CompressorUiCallbacks callbacks);
void compressorUiDestroy(CompressorUi* ui);
/* Pushes current parameter values into every widget. Never calls sendParams. */
void compressorUiSyncFromParams(CompressorUi* ui, const float* params);
/* Meter readings from the core (dB; gain reduction >= 0). UI thread, any rate. */
void compressorUiFeedMeters(CompressorUi* ui, float inputDb, float outputDb, float gainReductionDb);
