/*
 * VST3 build of the Glue on the VST3 SDK directly (#171), on the shared
 * shell (VSTs/common/vst3/LoudioVst3.h). No JUCE: GlueCore does the
 * audio, as in the LPI build (lpi_glue.cpp); on Windows the editor hosts
 * the plugin's cuif UI in a child window. Everything format-specific lives
 * in the shell; this file is only what makes it the Glue.
 */
#include "vst3/LoudioVst3.h"
#include "GlueCore.h"
#ifdef LOUDIO_VST3_EDITOR
#include "GlueUi.h"
#endif

struct GlueVst3Traits {
    using Core = GlueCore;

    static constexpr const char* kName = "Loudio Glue";
    static constexpr const char* kVendor = "Loudio";
    static constexpr const char* kUrl = "https://github.com/agentchieflou/v-loudio-t";
    static constexpr const char* kEmail = "";
    static constexpr const char* kCategories = "Fx|Dynamics";
    static constexpr Steinberg::uint32 kProcessorUid[4] = { 0x7708C69D, 0x813740A4, 0x8C434054, 0xCCF9E116 };
    static constexpr Steinberg::uint32 kControllerUid[4] = { 0x47E001B4, 0xE63547E7, 0xA2710104, 0x0E5F6718 };

    static std::vector<const char*> choiceLabels(int index) {
        std::vector<const char*> labels;
        if (index == kGlueRatio) for (int i = 0; i < kGlueNumRatios; ++i) labels.push_back(kGlueRatioLabels[i]);
        if (index == kGlueAttack) for (int i = 0; i < kGlueNumAttacks; ++i) labels.push_back(kGlueAttackLabels[i]);
        if (index == kGlueRelease) for (int i = 0; i < kGlueNumReleases; ++i) labels.push_back(kGlueReleaseLabels[i]);
        return labels;
    }

    /* One meter frame per block: in, out, gain reduction (dB). */
    static void afterProcess(Core& core, cuif_spsc_ring_buffer& dspToUi) {
        float frame[3] = { core.meterInputDb(), core.meterOutputDb(), core.meterGainReductionDb() };
        loudio::vst3::pushFrame(dspToUi, 'M', frame);
    }

#ifdef LOUDIO_VST3_EDITOR
    using Ui = GlueUi;

    static constexpr int kPanelWidth = kGluePanelWidth;
    static constexpr int kPanelHeight = kGluePanelHeight;
    static constexpr const char* kWindowTitle = "Loudio Glue";

    static Ui* uiCreate(float w, float h, void* context, loudio::vst3::SendParamsFn sendParams) {
        return glueUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { glueUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { glueUiSyncFromParams(ui, params); }

    static void pollDspToUi(Ui* ui, cuif_spsc_ring_buffer& fromDsp) {
        unsigned char tag;
        float frame[3];
        while (cuif_spsc_readable(&fromDsp) >= sizeof(tag) + sizeof(frame)) {
            if (cuif_spsc_read(&fromDsp, &tag, sizeof(tag)) != sizeof(tag)) break;
            cuif_spsc_read(&fromDsp, (unsigned char*)frame, sizeof(frame));
            if (tag == 'M') glueUiFeedMeters(ui, frame[0], frame[1], frame[2]);
        }
    }
#endif
};

LOUDIO_VST3_DEFINE_PLUGIN(GlueVst3Traits)
