#pragma once

#include "LoudioEditor.h" /* VSTs/common: LoudioProcessor + LoudioEditor templates */
#include "BusCore.h"
#include "BusUi.h"

/* The Bus's VST3/Standalone build on the shared JUCE wrapper (VSTs/common). */
struct BusVstTraits {
    using Core = BusCore;
    using Ui = BusUi;

    static constexpr int kPanelWidth = kBusPanelWidth;
    static constexpr int kPanelHeight = kBusPanelHeight;
    static constexpr const char* kWindowTitle = "Loudio Bus";

    static juce::StringArray choiceLabels(int) { return {}; }

    static Ui* uiCreate(float w, float h, void* context, loudio::vst::SendParamsFn sendParams) {
        return busUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { busUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { busUiSyncFromParams(ui, params); }

    /* One meter frame per block: in, out (dB). */
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
            if (tag == 'M') busUiFeedMeters(ui, frame[0], frame[1]);
        }
    }
};

using LoudioBusProcessor = loudio::vst::LoudioProcessor<BusVstTraits>;
using LoudioBusEditor = loudio::vst::LoudioEditor<BusVstTraits>;
