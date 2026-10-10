#pragma once

#include "LoudioEditor.h" /* VSTs/common: LoudioProcessor + LoudioEditor templates (#129) */
#include "ReverbCore.h"
#include "ReverbUi.h"

/*
 * The Reverb's VST3/Standalone build on the shared JUCE wrapper
 * (VSTs/common). Everything format-specific -- the APVTS layout from
 * kReverbParams, the UI<->DSP rings, the cuif child window, DPI debounce --
 * lives in the templates; this is only what makes it the Reverb.
 *
 * The APVTS XML preset shape is unchanged from the hand-written wrapper:
 * same ids, names, parameter types (Bool for freeze, Choice for mode and
 * uiTheme with the same labels, Float with the same ranges and steps) and
 * the same order, all of which now come from the table.
 */
struct ReverbVstTraits {
    using Core = ReverbCore;
    using Ui = ReverbUi;

    static constexpr int kPanelWidth = 800;
    static constexpr int kPanelHeight = 600;
    static constexpr const char* kWindowTitle = "Reverb Panel";

    static juce::StringArray choiceLabels(int index) {
        if (index == kParamMode) return { "Room", "Hall", "Plate", "Cathedral", "Spring" };
        if (index == kParamUiTheme) return { "Default", "Hello Kitty", "Greens" }; /* cosmetic, persisted like any other setting (#68) */
        return {};
    }

    static Ui* uiCreate(float w, float h, void* context, loudio::vst::SendParamsFn sendParams) {
        return reverbUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { reverbUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { reverbUiSyncFromParams(ui, params); }

    /* Analyzer frames, audio thread -> UI. If a host's block is large
     * enough for the 1024-sample FFT to complete more than once per block
     * (offline bounce), only the last completion is pushed; cosmetic only. */
    static void afterProcess(Core& core, cuif_spsc_ring_buffer& dspToUi) {
        float magnitudes[64];
        if (core.isLeftAnalyzerFrameDirty()) {
            core.getLeftAnalyzerMagnitudes(magnitudes);
            loudio::vst::pushFrame(dspToUi, 'L', magnitudes);
        }
        if (core.isRightAnalyzerFrameDirty()) {
            core.getRightAnalyzerMagnitudes(magnitudes);
            loudio::vst::pushFrame(dspToUi, 'R', magnitudes);
        }
    }

    static void pollDspToUi(Ui* ui, cuif_spsc_ring_buffer& fromDsp) {
        unsigned char tag;
        while (cuif_spsc_readable(&fromDsp) >= sizeof(tag) + sizeof(ui->leftSpectrum)) {
            if (cuif_spsc_read(&fromDsp, &tag, sizeof(tag)) != sizeof(tag)) break;
            float* dst = tag == 'L' ? ui->leftSpectrum : (tag == 'R' ? ui->rightSpectrum : nullptr);
            if (dst) {
                cuif_spsc_read(&fromDsp, (unsigned char*)dst, sizeof(ui->leftSpectrum));
            } else {
                float discard[64];
                cuif_spsc_read(&fromDsp, (unsigned char*)discard, sizeof(discard));
            }
        }
    }
};

using LoudioReverbProcessor = loudio::vst::LoudioProcessor<ReverbVstTraits>;
using LoudioReverbEditor = loudio::vst::LoudioEditor<ReverbVstTraits>;
