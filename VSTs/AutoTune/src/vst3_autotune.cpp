/*
 * VST3 build of the AutoTune on the VST3 SDK directly (#171), on the shared
 * shell (VSTs/common/vst3/LoudioVst3.h). No JUCE: AutoTuneCore does the
 * audio, as in the LPI build (lpi_autotune.cpp); on Windows the editor hosts
 * the plugin's cuif UI in a child window. Everything format-specific lives
 * in the shell; this file is only what makes it the AutoTune.
 */
#include "vst3/LoudioVst3.h"
#include "AutoTuneCore.h"
#ifdef LOUDIO_VST3_EDITOR
#include "AutoTuneUi.h"
#endif

struct AutoTuneVst3Traits {
    using Core = AutoTuneCore;

    static constexpr const char* kName = "Loudio AutoTune";
    static constexpr const char* kVendor = "Loudio";
    static constexpr const char* kUrl = "https://github.com/agentchieflou/v-loudio-t";
    static constexpr const char* kEmail = "";
    static constexpr const char* kCategories = "Fx|Pitch Shift";
    static constexpr Steinberg::uint32 kProcessorUid[4] = { 0x65F7A2CD, 0x33CA4747, 0x8D4E570F, 0x737C3266 };
    static constexpr Steinberg::uint32 kControllerUid[4] = { 0x950D3DE7, 0x9D10465D, 0x9421D451, 0x775FBD90 };

    static std::vector<const char*> choiceLabels(int index) {
        std::vector<const char*> labels;
        if (index == kAutoTuneKey) for (int i = 0; i < 12; ++i) labels.push_back(kAutoTuneKeyLabels[i]);
        if (index == kAutoTuneScale) for (int i = 0; i < kAutoTuneNumScales; ++i) labels.push_back(kAutoTuneScaleLabels[i]);
        if (index == kAutoTuneInputType) for (int i = 0; i < kAutoTuneNumInputTypes; ++i) labels.push_back(kAutoTuneInputLabels[i]);
        return labels;
    }

    /* One pitch frame per block: voiced, detected MIDI, target note, cents. */
    static void afterProcess(Core& core, cuif_spsc_ring_buffer& dspToUi) {
        float frame[4] = { core.detectedVoiced() ? 1.0f : 0.0f, core.detectedMidi(), (float)core.targetMidi(), core.correctionCents() };
        loudio::vst3::pushFrame(dspToUi, 'P', frame);
    }

#ifdef LOUDIO_VST3_EDITOR
    using Ui = AutoTuneUi;

    static constexpr int kPanelWidth = kAutoTunePanelWidth;
    static constexpr int kPanelHeight = kAutoTunePanelHeight;
    static constexpr const char* kWindowTitle = "Loudio AutoTune";

    static Ui* uiCreate(float w, float h, void* context, loudio::vst3::SendParamsFn sendParams) {
        return autoTuneUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { autoTuneUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { autoTuneUiSyncFromParams(ui, params); }

    static void pollDspToUi(Ui* ui, cuif_spsc_ring_buffer& fromDsp) {
        unsigned char tag;
        float frame[4];
        while (cuif_spsc_readable(&fromDsp) >= sizeof(tag) + sizeof(frame)) {
            if (cuif_spsc_read(&fromDsp, &tag, sizeof(tag)) != sizeof(tag)) break;
            cuif_spsc_read(&fromDsp, (unsigned char*)frame, sizeof(frame));
            if (tag == 'P') autoTuneUiFeedPitch(ui, frame[0] > 0.5f, frame[1], (int)frame[2], frame[3]);
        }
    }
#endif
};

LOUDIO_VST3_DEFINE_PLUGIN(AutoTuneVst3Traits)
