#pragma once

#include "AutoTuneParams.h"
#include "cuif/cuif.h"
#include <cstddef>

/*
 * AutoTune widget tree (#146), shared by the VST3 editor (VSTs/common)
 * and the LPI offscreen editor (PluginABI/wrapper). JUCE-free; UI thread
 * only.
 *
 * Layout (logical px, 660 x 240): key, scale and input-type dropdowns on
 * row 1 with the detected-note readout (a mouse-disabled button whose
 * label shows e.g. "A4 +12c") on the right; retune speed, humanise,
 * formant, mix and reference knobs on row 2 with a correction meter
 * (cents, filling from the top) at the far right.
 */
typedef void (*AutoTuneSendParamsFn)(void* context, const loudio::ParamUpdate* updates, size_t count);

struct AutoTuneUiCallbacks {
    void* context;
    AutoTuneSendParamsFn sendParams;
};

constexpr int kAutoTunePanelWidth = 660;
constexpr int kAutoTunePanelHeight = 240;
constexpr float kAutoTuneRow1Y = 20.0f, kAutoTuneDropH = 28.0f;
constexpr float kAutoTuneKeyX = 20.0f, kAutoTuneKeyW = 70.0f;
constexpr float kAutoTuneScaleX = 100.0f, kAutoTuneScaleW = 130.0f;
constexpr float kAutoTuneTypeX = 240.0f, kAutoTuneTypeW = 120.0f;
constexpr float kAutoTuneNoteX = 500.0f, kAutoTuneNoteY = 20.0f, kAutoTuneNoteW = 140.0f, kAutoTuneNoteH = 40.0f;
constexpr float kAutoTuneKnobSize = 80.0f, kAutoTuneKnobX0 = 20.0f, kAutoTuneKnobDx = 90.0f, kAutoTuneKnobY = 80.0f;
constexpr float kAutoTuneMeterX = 600.0f, kAutoTuneMeterY = 80.0f, kAutoTuneMeterW = 22.0f, kAutoTuneMeterH = 140.0f;
constexpr float kAutoTuneMeterRangeCents = 50.0f;
/* The GUI harness drags the Retune Speed knob (first knob) at its centre. */
constexpr float kAutoTuneRetuneKnobX = kAutoTuneKnobX0, kAutoTuneRetuneKnobY = kAutoTuneKnobY;

struct AutoTuneUi {
    AutoTuneUiCallbacks callbacks;
    cuif_widget* root = nullptr;
    cuif_widget* knobs[kAutoTuneNumParams] = {};
    cuif_widget* keyDropdown = nullptr;
    cuif_widget* scaleDropdown = nullptr;
    cuif_widget* typeDropdown = nullptr;
    cuif_widget* noteReadout = nullptr;
    cuif_widget* centsMeter = nullptr;
};

AutoTuneUi* autoTuneUiCreate(float panelWidth, float panelHeight, AutoTuneUiCallbacks callbacks);
void autoTuneUiDestroy(AutoTuneUi* ui);
/* Pushes current parameter values into every widget. Never calls sendParams. */
void autoTuneUiSyncFromParams(AutoTuneUi* ui, const float* params);
/* The last analysis frame: detected MIDI pitch (fractional), target note and
 * applied correction in cents; voiced false blanks the readout. UI thread. */
void autoTuneUiFeedPitch(AutoTuneUi* ui, bool voiced, float detectedMidi, int targetNote, float correctionCents);
