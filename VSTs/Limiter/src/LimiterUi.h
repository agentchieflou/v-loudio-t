#pragma once

#include "LimiterParams.h"
#include "cuif/cuif.h"
#include <cstddef>

/*
 * Limiter widget tree (#139), shared by the VST3 editor (VSTs/common/vst3) and
 * the LPI offscreen editor (PluginABI/wrapper). JUCE-free; UI thread only.
 *
 * Layout (logical px, 620 x 260): input / gain-reduction / output meters
 * on the left; knobs: ceiling, release, lookahead, input gain, link; the
 * oversampling dropdown under them. Hosts feed the meters with
 * limiterUiFeedMeters() from the core's per-block readings.
 */
typedef void (*LimiterSendParamsFn)(void* context, const loudio::ParamUpdate* updates, size_t count);

struct LimiterUiCallbacks {
    void* context;
    LimiterSendParamsFn sendParams;
};

constexpr int kLimiterPanelWidth = 620;
constexpr int kLimiterPanelHeight = 260;
constexpr float kLimiterMeterY = 20.0f, kLimiterMeterW = 22.0f, kLimiterMeterH = 220.0f;
constexpr float kLimiterInMeterX = 20.0f, kLimiterGrMeterX = 52.0f, kLimiterOutMeterX = 84.0f;
constexpr float kLimiterKnobSize = 80.0f, kLimiterKnobX0 = 140.0f, kLimiterKnobDx = 90.0f, kLimiterKnobY = 20.0f;
constexpr float kLimiterOsX = 140.0f, kLimiterOsY = 150.0f, kLimiterOsW = 120.0f, kLimiterOsH = 28.0f;
/* The GUI harness drags the Ceiling knob (first) at its centre. */
constexpr float kLimiterCeilingKnobX = kLimiterKnobX0, kLimiterCeilingKnobY = kLimiterKnobY;
constexpr float kLimiterMeterMinDb = -60.0f, kLimiterGrMeterRangeDb = 24.0f;

struct LimiterUi {
    LimiterUiCallbacks callbacks;
    cuif_widget* root = nullptr;
    cuif_widget* knobs[kLimiterNumParams] = {};
    cuif_widget* osDropdown = nullptr;
    cuif_widget* inMeter = nullptr;
    cuif_widget* grMeter = nullptr;
    cuif_widget* outMeter = nullptr;
};

LimiterUi* limiterUiCreate(float panelWidth, float panelHeight, LimiterUiCallbacks callbacks);
void limiterUiDestroy(LimiterUi* ui);
/* Pushes current parameter values into every widget. Never calls sendParams. */
void limiterUiSyncFromParams(LimiterUi* ui, const float* params);
/* Meter readings from the core (dB; gain reduction >= 0). UI thread, any rate. */
void limiterUiFeedMeters(LimiterUi* ui, float inputDb, float outputDb, float gainReductionDb);
