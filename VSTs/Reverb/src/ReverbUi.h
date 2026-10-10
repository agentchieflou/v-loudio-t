#pragma once

#include "SharedStructures.h"
#include "cuif/cuif.h"
#include <cstddef>

/*
 * The Reverb editor's widget tree, built once here and shared by both
 * editors: the VST3/JUCE editor (VSTs/common/LoudioEditor.h, native child window) and
 * the LPI wrapper's offscreen editor (lpi_reverb.cpp). JUCE-free.
 *
 * The UI never writes parameters itself. Every edit goes out through
 * ReverbUiCallbacks::sendParams, so each host keeps its own RT-safe path to
 * the DSP (the JUCE editor's all-or-nothing UI->DSP ring buffer; the LPI
 * wrapper's ReverbCore::setParameterValue queue). sendParams may receive
 * several messages that belong together (the decay-EQ curve's three nodes)
 * and should deliver all of them or none.
 *
 * Threading: everything here runs on the host's UI thread.
 */
struct ReverbUiCallbacks {
    void* context;
    void (*sendParams)(void* context, const ParamUpdateMessage* messages, size_t count);
};

struct ReverbUi {
    ReverbUiCallbacks callbacks;

    cuif_widget* root = nullptr;
    cuif_widget* mainPage = nullptr;
    cuif_widget* advancedPage = nullptr;
    cuif_widget* tabBar = nullptr;
    cuif_widget* knobs[kNumParams] = {};
    cuif_widget* freezeButton = nullptr;
    cuif_widget* modeDropdown = nullptr;
    cuif_widget* themeDropdown = nullptr;
    cuif_widget* bezierEditor = nullptr;
    cuif_widget* analyzerLeft = nullptr;
    cuif_widget* analyzerRight = nullptr;

    /* The analyzer widgets draw straight from these; hosts copy fresh
     * magnitudes in (any rate, UI thread only). */
    float leftSpectrum[64] = {};
    float rightSpectrum[64] = {};
};

/* Builds the full widget tree at the given panel size. The caller attaches
 * ui->root to its cuif window and owns the result: free it with
 * reverbUiDestroy(). Returns nullptr on allocation failure. */
ReverbUi* reverbUiCreate(float panelWidth, float panelHeight, ReverbUiCallbacks callbacks);

/* Destroys the widget tree and the ReverbUi. Detach ui->root from any
 * window first. Safe on nullptr. */
void reverbUiDestroy(ReverbUi* ui);

/* Pushes current parameter values (kNumParams floats, indexed by
 * ReverbParamIndex) into every widget, and applies the UI theme. Never
 * calls sendParams. */
void reverbUiSyncFromParams(ReverbUi* ui, const float* params);
