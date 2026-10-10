#pragma once

#include "LoudioEditor.h" /* VSTs/common: LoudioProcessor + LoudioEditor templates */
#include "LimiterCore.h"
#include "LimiterUi.h"

/* The Limiter's VST3/Standalone build on the shared JUCE wrapper (VSTs/common). */
struct LimiterVstTraits {
    using Core = LimiterCore;
    using Ui = LimiterUi;

    static constexpr int kPanelWidth = kLimiterPanelWidth;
    static constexpr int kPanelHeight = kLimiterPanelHeight;
    static constexpr const char* kWindowTitle = "Loudio Limiter";

    static juce::StringArray choiceLabels(int index) {
        if (index == kLimiterOversampling) return { "Off", "2x", "4x" };
        return {};
    }

    static Ui* uiCreate(float w, float h, void* context, loudio::vst::SendParamsFn sendParams) {
        return limiterUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { limiterUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { limiterUiSyncFromParams(ui, params); }

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
            if (tag == 'M') limiterUiFeedMeters(ui, frame[0], frame[1], frame[2]);
        }
    }
};

using LoudioLimiterProcessor = loudio::vst::LoudioProcessor<LimiterVstTraits>;
using LoudioLimiterEditor = loudio::vst::LoudioEditor<LimiterVstTraits>;
