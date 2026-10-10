#pragma once

#include "LoudioEditor.h" /* VSTs/common: LoudioProcessor + LoudioEditor templates */
#include "ScratchCore.h"
#include "ScratchUi.h"

/*
 * Scratch's VST3/Standalone build on the shared JUCE wrapper (VSTs/common).
 * The APVTS layout, rings, cuif child window and DPI handling live in the
 * templates; this is only what makes it Scratch.
 */
struct ScratchVstTraits {
    using Core = ScratchCore;
    using Ui = ScratchUi;

    static constexpr int kPanelWidth = kScratchPanelWidth;
    static constexpr int kPanelHeight = kScratchPanelHeight;
    static constexpr const char* kWindowTitle = "Loudio Scratch";

    /* Labels for kParamStepped parameters; none in the scaffold. */
    static juce::StringArray choiceLabels(int) { return {}; }

    static Ui* uiCreate(float w, float h, void* context, loudio::vst::SendParamsFn sendParams) {
        return scratchUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { scratchUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { scratchUiSyncFromParams(ui, params); }

    /* DSP -> UI frames (meters, analyzers): none in the scaffold. */
    static void afterProcess(Core&, cuif_spsc_ring_buffer&) {}
    static void pollDspToUi(Ui*, cuif_spsc_ring_buffer&) {}
};

using LoudioScratchProcessor = loudio::vst::LoudioProcessor<ScratchVstTraits>;
using LoudioScratchEditor = loudio::vst::LoudioEditor<ScratchVstTraits>;
