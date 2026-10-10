#pragma once

#include "LoudioEditor.h" /* VSTs/common: LoudioProcessor + LoudioEditor templates */
#include "EQ3Core.h"
#include "EQ3Ui.h"

/* The EQ-3's VST3/Standalone build on the shared JUCE wrapper (VSTs/common). */
struct EQ3VstTraits {
    using Core = EQ3Core;
    using Ui = EQ3Ui;

    static constexpr int kPanelWidth = kEQ3PanelWidth;
    static constexpr int kPanelHeight = kEQ3PanelHeight;
    static constexpr const char* kWindowTitle = "Loudio EQ-3";

    static juce::StringArray choiceLabels(int) { return {}; }

    static Ui* uiCreate(float w, float h, void* context, loudio::vst::SendParamsFn sendParams) {
        return eq3UiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { eq3UiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { eq3UiSyncFromParams(ui, params); }

    static void afterProcess(Core&, cuif_spsc_ring_buffer&) {}
    static void pollDspToUi(Ui*, cuif_spsc_ring_buffer&) {}
};

using LoudioEQ3Processor = loudio::vst::LoudioProcessor<EQ3VstTraits>;
using LoudioEQ3Editor = loudio::vst::LoudioEditor<EQ3VstTraits>;
