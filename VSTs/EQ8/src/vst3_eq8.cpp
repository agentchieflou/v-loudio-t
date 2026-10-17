/*
 * VST3 build of the EQ8 on the VST3 SDK directly (#171), on the shared
 * shell (VSTs/common/vst3/LoudioVst3.h). No JUCE: EQ8Core does the
 * audio, as in the LPI build (lpi_eq8.cpp); on Windows the editor hosts
 * the plugin's cuif UI in a child window. Everything format-specific lives
 * in the shell; this file is only what makes it the EQ8.
 */
#include "vst3/LoudioVst3.h"
#include "EQ8Core.h"
#ifdef LOUDIO_VST3_EDITOR
#include "EQ8Ui.h"
#endif

struct EQ8Vst3Traits {
    using Core = EQ8Core;

    static constexpr const char* kName = "Loudio EQ-8";
    static constexpr const char* kVendor = "Loudio";
    static constexpr const char* kUrl = "https://github.com/agentchieflou/v-loudio-t";
    static constexpr const char* kEmail = "";
    static constexpr const char* kCategories = "Fx|EQ";
    static constexpr Steinberg::uint32 kProcessorUid[4] = { 0xBCD74CFE, 0x7640477C, 0x81E8183F, 0x73022399 };
    static constexpr Steinberg::uint32 kControllerUid[4] = { 0x4ACAA640, 0x67BC4970, 0x9A0D5DA4, 0x92D11619 };

    static std::vector<const char*> choiceLabels(int index) {
        if (kEQ8Params[index].flags & loudio::kParamStepped) {
            std::vector<const char*> labels;
            for (int i = 0; i < kEQ8NumTypes; ++i) labels.push_back(kEQ8TypeLabels[i]);
            return labels;
        }
        return {};
    }

    static void afterProcess(Core& core, cuif_spsc_ring_buffer& dspToUi) {
        if (core.isAnalyzerFrameDirty()) {
            float magnitudes[64];
            core.getAnalyzerMagnitudes(magnitudes);
            loudio::vst3::pushFrame(dspToUi, 'A', magnitudes);
        }
    }

#ifdef LOUDIO_VST3_EDITOR
    using Ui = EQ8Ui;

    static constexpr int kPanelWidth = kEQ8PanelWidth;
    static constexpr int kPanelHeight = kEQ8PanelHeight;
    static constexpr const char* kWindowTitle = "Loudio EQ-8";

    static Ui* uiCreate(float w, float h, void* context, loudio::vst3::SendParamsFn sendParams) {
        return eq8UiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { eq8UiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { eq8UiSyncFromParams(ui, params); }

    static void pollDspToUi(Ui* ui, cuif_spsc_ring_buffer& fromDsp) {
        unsigned char tag;
        while (cuif_spsc_readable(&fromDsp) >= sizeof(tag) + sizeof(ui->spectrum)) {
            if (cuif_spsc_read(&fromDsp, &tag, sizeof(tag)) != sizeof(tag)) break;
            float discard[64];
            float* dst = tag == 'A' ? ui->spectrum : discard;
            cuif_spsc_read(&fromDsp, (unsigned char*)dst, sizeof(ui->spectrum));
        }
    }
#endif
};

LOUDIO_VST3_DEFINE_PLUGIN(EQ8Vst3Traits)
