#pragma once

#include "LoudioEditor.h" /* VSTs/common: LoudioProcessor + LoudioEditor templates */
#include "PhaserCore.h"
#include "PhaserUi.h"

/* The Phaser's VST3/Standalone build on the shared JUCE wrapper (VSTs/common). */
struct PhaserVstTraits {
    using Core = PhaserCore;
    using Ui = PhaserUi;

    static constexpr int kPanelWidth = kPhaserPanelWidth;
    static constexpr int kPanelHeight = kPhaserPanelHeight;
    static constexpr const char* kWindowTitle = "Loudio Phaser";

    static juce::StringArray choiceLabels(int index) {
        if (index == kPhaserStages) return { "4", "6", "8", "12" };
        return {};
    }

    static Ui* uiCreate(float w, float h, void* context, loudio::vst::SendParamsFn sendParams) {
        return phaserUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { phaserUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { phaserUiSyncFromParams(ui, params); }

    static void afterProcess(Core&, cuif_spsc_ring_buffer&) {}
    static void pollDspToUi(Ui*, cuif_spsc_ring_buffer&) {}
};

using LoudioPhaserProcessor = loudio::vst::LoudioProcessor<PhaserVstTraits>;
using LoudioPhaserEditor = loudio::vst::LoudioEditor<PhaserVstTraits>;
