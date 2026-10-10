#pragma once

#include "LoudioEditor.h" /* VSTs/common: LoudioProcessor + LoudioEditor templates */
#include "AutoTuneCore.h"
#include "AutoTuneUi.h"

/* The AutoTune's VST3/Standalone build on the shared JUCE wrapper (VSTs/common). */
struct AutoTuneVstTraits {
    using Core = AutoTuneCore;
    using Ui = AutoTuneUi;

    static constexpr int kPanelWidth = kAutoTunePanelWidth;
    static constexpr int kPanelHeight = kAutoTunePanelHeight;
    static constexpr const char* kWindowTitle = "Loudio AutoTune";

    static juce::StringArray choiceLabels(int index) {
        juce::StringArray labels;
        if (index == kAutoTuneKey) for (int i = 0; i < 12; ++i) labels.add(kAutoTuneKeyLabels[i]);
        if (index == kAutoTuneScale) for (int i = 0; i < kAutoTuneNumScales; ++i) labels.add(kAutoTuneScaleLabels[i]);
        if (index == kAutoTuneInputType) for (int i = 0; i < kAutoTuneNumInputTypes; ++i) labels.add(kAutoTuneInputLabels[i]);
        return labels;
    }

    static Ui* uiCreate(float w, float h, void* context, loudio::vst::SendParamsFn sendParams) {
        return autoTuneUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { autoTuneUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { autoTuneUiSyncFromParams(ui, params); }

    /* One pitch frame per block: voiced, detected MIDI, target note, cents. */
    static void afterProcess(Core& core, cuif_spsc_ring_buffer& dspToUi) {
        float frame[4] = { core.detectedVoiced() ? 1.0f : 0.0f, core.detectedMidi(), (float)core.targetMidi(), core.correctionCents() };
        loudio::vst::pushFrame(dspToUi, 'P', frame);
    }

    static void pollDspToUi(Ui* ui, cuif_spsc_ring_buffer& fromDsp) {
        unsigned char tag;
        float frame[4];
        while (cuif_spsc_readable(&fromDsp) >= sizeof(tag) + sizeof(frame)) {
            if (cuif_spsc_read(&fromDsp, &tag, sizeof(tag)) != sizeof(tag)) break;
            cuif_spsc_read(&fromDsp, (unsigned char*)frame, sizeof(frame));
            if (tag == 'P') autoTuneUiFeedPitch(ui, frame[0] > 0.5f, frame[1], (int)frame[2], frame[3]);
        }
    }
};

using LoudioAutoTuneProcessor = loudio::vst::LoudioProcessor<AutoTuneVstTraits>;
using LoudioAutoTuneEditor = loudio::vst::LoudioEditor<AutoTuneVstTraits>;
