#pragma once

#include "LoudioEditor.h" /* VSTs/common: LoudioProcessor + LoudioEditor templates */
#include "GlueCore.h"
#include "GlueUi.h"

/* The Glue's VST3/Standalone build on the shared JUCE wrapper (VSTs/common). */
struct GlueVstTraits {
    using Core = GlueCore;
    using Ui = GlueUi;

    static constexpr int kPanelWidth = kGluePanelWidth;
    static constexpr int kPanelHeight = kGluePanelHeight;
    static constexpr const char* kWindowTitle = "Loudio Glue";

    static juce::StringArray choiceLabels(int index) {
        juce::StringArray labels;
        if (index == kGlueRatio) for (int i = 0; i < kGlueNumRatios; ++i) labels.add(kGlueRatioLabels[i]);
        if (index == kGlueAttack) for (int i = 0; i < kGlueNumAttacks; ++i) labels.add(kGlueAttackLabels[i]);
        if (index == kGlueRelease) for (int i = 0; i < kGlueNumReleases; ++i) labels.add(kGlueReleaseLabels[i]);
        return labels;
    }

    static Ui* uiCreate(float w, float h, void* context, loudio::vst::SendParamsFn sendParams) {
        return glueUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { glueUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { glueUiSyncFromParams(ui, params); }

    /* One meter frame per block: in, out, gain reduction (dB). */
    static void afterProcess(Core& core, cuif_spsc_ring_buffer& dspToUi) {
        float frame[3] = { core.meterInputDb(), core.meterOutputDb(), core.meterGainReductionDb() };
        loudio::vst::pushFrame(dspToUi, 'M', frame);
    }

    static void pollDspToUi(Ui* ui, cuif_spsc_ring_buffer& fromDsp) {
        unsigned char tag;
        float frame[3];
        while (cuif_spsc_readable(&fromDsp) >= sizeof(tag) + sizeof(frame)) {
            if (cuif_spsc_read(&fromDsp, &tag, sizeof(tag)) != sizeof(tag)) break;
            cuif_spsc_read(&fromDsp, (unsigned char*)frame, sizeof(frame));
            if (tag == 'M') glueUiFeedMeters(ui, frame[0], frame[1], frame[2]);
        }
    }
};

using LoudioGlueProcessor = loudio::vst::LoudioProcessor<GlueVstTraits>;
using LoudioGlueEditor = loudio::vst::LoudioEditor<GlueVstTraits>;
