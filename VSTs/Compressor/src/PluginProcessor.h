#pragma once

#include "LoudioEditor.h" /* VSTs/common: LoudioProcessor + LoudioEditor templates */
#include "CompressorCore.h"
#include "CompressorUi.h"

/*
 * The Compressor's VST3/Standalone build on the shared JUCE wrapper
 * (VSTs/common). The APVTS layout, rings, cuif child window, DPI handling
 * and latency reporting live in the templates; this is only what makes it
 * the Compressor.
 */
struct CompressorVstTraits {
    using Core = CompressorCore;
    using Ui = CompressorUi;

    static constexpr int kPanelWidth = kCompressorPanelWidth;
    static constexpr int kPanelHeight = kCompressorPanelHeight;
    static constexpr const char* kWindowTitle = "Loudio Compressor";

    static juce::StringArray choiceLabels(int index) {
        if (index == kCompressorDetector) return { "Peak", "RMS" };
        return {};
    }

    static Ui* uiCreate(float w, float h, void* context, loudio::vst::SendParamsFn sendParams) {
        return compressorUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { compressorUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { compressorUiSyncFromParams(ui, params); }

    /* One meter frame per block: in, out, gain reduction (dB). Dropped
     * when the ring is full, which only costs a meter update. */
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
            if (tag == 'M') compressorUiFeedMeters(ui, frame[0], frame[1], frame[2]);
        }
    }
};

using LoudioCompressorProcessor = loudio::vst::LoudioProcessor<CompressorVstTraits>;
using LoudioCompressorEditor = loudio::vst::LoudioEditor<CompressorVstTraits>;
