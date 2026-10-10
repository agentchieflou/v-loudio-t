#pragma once

#include "LoudioEditor.h" /* VSTs/common: LoudioProcessor + LoudioEditor templates */
#include "FlangerCore.h"
#include "FlangerUi.h"

/* The Flanger's VST3/Standalone build on the shared JUCE wrapper (VSTs/common). */
struct FlangerVstTraits {
    using Core = FlangerCore;
    using Ui = FlangerUi;

    static constexpr int kPanelWidth = kFlangerPanelWidth;
    static constexpr int kPanelHeight = kFlangerPanelHeight;
    static constexpr const char* kWindowTitle = "Loudio Flanger";

    static juce::StringArray choiceLabels(int) { return {}; }

    static Ui* uiCreate(float w, float h, void* context, loudio::vst::SendParamsFn sendParams) {
        return flangerUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { flangerUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { flangerUiSyncFromParams(ui, params); }

    static void afterProcess(Core&, cuif_spsc_ring_buffer&) {}
    static void pollDspToUi(Ui*, cuif_spsc_ring_buffer&) {}
};

using LoudioFlangerProcessor = loudio::vst::LoudioProcessor<FlangerVstTraits>;
using LoudioFlangerEditor = loudio::vst::LoudioEditor<FlangerVstTraits>;
