#pragma once

#include "LoudioEditor.h" /* VSTs/common: LoudioProcessor + LoudioEditor templates */
#include "DelayCore.h"
#include "DelayUi.h"

/* The Delay's VST3/Standalone build on the shared JUCE wrapper
 * (VSTs/common); the wrapper feeds setTransport() from the playhead. */
struct DelayVstTraits {
    using Core = DelayCore;
    using Ui = DelayUi;

    static constexpr int kPanelWidth = kDelayPanelWidth;
    static constexpr int kPanelHeight = kDelayPanelHeight;
    static constexpr const char* kWindowTitle = "Loudio Delay";

    static juce::StringArray choiceLabels(int index) {
        juce::StringArray labels;
        if (index == kDelayDivisionL || index == kDelayDivisionR) for (int i = 0; i < kDelayNumDivisions; ++i) labels.add(kDelayDivisionLabels[i]);
        return labels;
    }

    static Ui* uiCreate(float w, float h, void* context, loudio::vst::SendParamsFn sendParams) {
        return delayUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { delayUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { delayUiSyncFromParams(ui, params); }

    static void afterProcess(Core& core, cuif_spsc_ring_buffer& dspToUi) {
        float frame[2] = { core.meterInputDb(), core.meterOutputDb() };
        loudio::vst::pushFrame(dspToUi, 'M', frame);
    }

    static void pollDspToUi(Ui* ui, cuif_spsc_ring_buffer& fromDsp) {
        unsigned char tag;
        float frame[2];
        while (cuif_spsc_readable(&fromDsp) >= sizeof(tag) + sizeof(frame)) {
            if (cuif_spsc_read(&fromDsp, &tag, sizeof(tag)) != sizeof(tag)) break;
            cuif_spsc_read(&fromDsp, (unsigned char*)frame, sizeof(frame));
            if (tag == 'M') delayUiFeedMeters(ui, frame[0], frame[1]);
        }
    }
};

using LoudioDelayProcessor = loudio::vst::LoudioProcessor<DelayVstTraits>;
using LoudioDelayEditor = loudio::vst::LoudioEditor<DelayVstTraits>;
