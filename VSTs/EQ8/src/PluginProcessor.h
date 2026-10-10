#pragma once

#include "LoudioEditor.h" /* VSTs/common: LoudioProcessor + LoudioEditor templates */
#include "EQ8Core.h"
#include "EQ8Ui.h"

/* The EQ-8's VST3/Standalone build on the shared JUCE wrapper (VSTs/common). */
struct EQ8VstTraits {
    using Core = EQ8Core;
    using Ui = EQ8Ui;

    static constexpr int kPanelWidth = kEQ8PanelWidth;
    static constexpr int kPanelHeight = kEQ8PanelHeight;
    static constexpr const char* kWindowTitle = "Loudio EQ-8";

    static juce::StringArray choiceLabels(int index) {
        if (kEQ8Params[index].flags & loudio::kParamStepped) {
            juce::StringArray labels;
            for (int i = 0; i < kEQ8NumTypes; ++i) labels.add(kEQ8TypeLabels[i]);
            return labels;
        }
        return {};
    }

    static Ui* uiCreate(float w, float h, void* context, loudio::vst::SendParamsFn sendParams) {
        return eq8UiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { eq8UiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { eq8UiSyncFromParams(ui, params); }

    static void afterProcess(Core& core, cuif_spsc_ring_buffer& dspToUi) {
        if (core.isAnalyzerFrameDirty()) {
            float magnitudes[64];
            core.getAnalyzerMagnitudes(magnitudes);
            loudio::vst::pushFrame(dspToUi, 'A', magnitudes);
        }
    }

    static void pollDspToUi(Ui* ui, cuif_spsc_ring_buffer& fromDsp) {
        unsigned char tag;
        while (cuif_spsc_readable(&fromDsp) >= sizeof(tag) + sizeof(ui->spectrum)) {
            if (cuif_spsc_read(&fromDsp, &tag, sizeof(tag)) != sizeof(tag)) break;
            float discard[64];
            float* dst = tag == 'A' ? ui->spectrum : discard;
            cuif_spsc_read(&fromDsp, (unsigned char*)dst, sizeof(ui->spectrum));
        }
    }
};

using LoudioEQ8Processor = loudio::vst::LoudioProcessor<EQ8VstTraits>;
using LoudioEQ8Editor = loudio::vst::LoudioEditor<EQ8VstTraits>;
