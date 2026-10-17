#pragma once

#include "PhaserParams.h"
#include "cuif/cuif.h"
#include <cstddef>

/*
 * Phaser widget tree (#144), shared by the VST3 editor (VSTs/common/vst3) and
 * the LPI offscreen editor (PluginABI/wrapper). JUCE-free; UI thread only.
 *
 * Layout (logical px, 580 x 220): rate, depth, centre, feedback, stereo
 * phase and mix knobs in a row; the stages dropdown below.
 */
typedef void (*PhaserSendParamsFn)(void* context, const loudio::ParamUpdate* updates, size_t count);

struct PhaserUiCallbacks {
    void* context;
    PhaserSendParamsFn sendParams;
};

constexpr int kPhaserPanelWidth = 580;
constexpr int kPhaserPanelHeight = 220;
constexpr float kPhaserKnobSize = 80.0f, kPhaserKnobX0 = 20.0f, kPhaserKnobDx = 90.0f, kPhaserKnobY = 20.0f;
constexpr float kPhaserStagesX = 20.0f, kPhaserStagesY = 150.0f, kPhaserStagesW = 120.0f, kPhaserStagesH = 28.0f;
/* The GUI harness drags the Rate knob (first) at its centre. */
constexpr float kPhaserRateKnobX = kPhaserKnobX0, kPhaserRateKnobY = kPhaserKnobY;

struct PhaserUi {
    PhaserUiCallbacks callbacks;
    cuif_widget* root = nullptr;
    cuif_widget* knobs[kPhaserNumParams] = {};
    cuif_widget* stagesDropdown = nullptr;
};

PhaserUi* phaserUiCreate(float panelWidth, float panelHeight, PhaserUiCallbacks callbacks);
void phaserUiDestroy(PhaserUi* ui);
/* Pushes current parameter values into every widget. Never calls sendParams. */
void phaserUiSyncFromParams(PhaserUi* ui, const float* params);
