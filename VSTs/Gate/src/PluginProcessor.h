#pragma once

#include "LoudioEditor.h" /* VSTs/common: LoudioProcessor + LoudioEditor templates */
#include "GateCore.h"
#include "GateUi.h"

/*
 * The Gate's VST3/Standalone build on the shared JUCE wrapper
 * (VSTs/common). The APVTS layout, rings, cuif child window, DPI handling
 * and latency reporting live in the templates; this is only what makes
 * it the Gate.
 */
struct GateVstTraits {
    using Core = GateCore;
    using Ui = GateUi;

    static constexpr int kPanelWidth = kGatePanelWidth;
    static constexpr int kPanelHeight = kGatePanelHeight;
    static constexpr const char* kWindowTitle = "Loudio Gate";

    static juce::StringArray choiceLabels(int) { return {}; }

    static Ui* uiCreate(float w, float h, void* context, loudio::vst::SendParamsFn sendParams) {
        return gateUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { gateUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { gateUiSyncFromParams(ui, params); }

    /* One meter frame per block: input dB, gain reduction dB, open (1/0). */
    static void afterProcess(Core& core, cuif_spsc_ring_buffer& dspToUi) {
        float frame[3] = { core.meterInputDb(), core.meterGainReductionDb(), core.meterOpen() ? 1.0f : 0.0f };
        loudio::vst::pushFrame(dspToUi, 'M', frame);
    }

    static void pollDspToUi(Ui* ui, cuif_spsc_ring_buffer& fromDsp) {
        unsigned char tag;
        float frame[3];
        while (cuif_spsc_readable(&fromDsp) >= sizeof(tag) + sizeof(frame)) {
            if (cuif_spsc_read(&fromDsp, &tag, sizeof(tag)) != sizeof(tag)) break;
            cuif_spsc_read(&fromDsp, (unsigned char*)frame, sizeof(frame));
            if (tag == 'M') gateUiFeedMeters(ui, frame[0], frame[1], frame[2] > 0.5f);
        }
    }
};

using LoudioGateProcessor = loudio::vst::LoudioProcessor<GateVstTraits>;
using LoudioGateEditor = loudio::vst::LoudioEditor<GateVstTraits>;
