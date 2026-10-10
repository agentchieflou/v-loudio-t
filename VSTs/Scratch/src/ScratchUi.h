#pragma once

#include "ScratchParams.h"
#include "cuif/cuif.h"
#include <cstddef>

/*
 * Scratch's widget tree, shared by the VST3 editor (VSTs/common) and the
 * LPI offscreen editor (PluginABI/wrapper). JUCE-free; UI thread only.
 *
 * The UI never writes parameters itself: every edit goes out through
 * sendParams (batched where edits belong together) and each host keeps
 * its own RT-safe path to the core.
 */
typedef void (*ScratchSendParamsFn)(void* context, const loudio::ParamUpdate* updates, size_t count);

struct ScratchUiCallbacks {
    void* context;
    ScratchSendParamsFn sendParams;
};

/* Layout in logical pixels. The GUI harness drags the gain knob at its
 * centre, so keep these in sync with test/lpi_scratch_gui_harness.c. */
constexpr int kScratchPanelWidth = 400;
constexpr int kScratchPanelHeight = 240;
constexpr float kScratchGainKnobX = 150.0f, kScratchGainKnobY = 60.0f, kScratchGainKnobSize = 100.0f;

struct ScratchUi {
    ScratchUiCallbacks callbacks;
    cuif_widget* root = nullptr;
    cuif_widget* knobs[kScratchNumParams] = {};
};

ScratchUi* scratchUiCreate(float panelWidth, float panelHeight, ScratchUiCallbacks callbacks);
void scratchUiDestroy(ScratchUi* ui);
/* Pushes current parameter values into every widget. Never calls sendParams. */
void scratchUiSyncFromParams(ScratchUi* ui, const float* params);
