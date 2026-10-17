/*
 * VST3 build of the Delay on the VST3 SDK directly (#171), on the shared
 * shell (VSTs/common/vst3/LoudioVst3.h). No JUCE: DelayCore does the
 * audio, as in the LPI build (lpi_delay.cpp); on Windows the editor hosts
 * the plugin's cuif UI in a child window. Everything format-specific lives
 * in the shell; this file is only what makes it the Delay.
 */
#include "vst3/LoudioVst3.h"
#include "DelayCore.h"
#ifdef LOUDIO_VST3_EDITOR
#include "DelayUi.h"
#endif

struct DelayVst3Traits {
    using Core = DelayCore;

    static constexpr const char* kName = "Loudio Delay";
    static constexpr const char* kVendor = "Loudio";
    static constexpr const char* kUrl = "https://github.com/agentchieflou/v-loudio-t";
    static constexpr const char* kEmail = "";
    static constexpr const char* kCategories = "Fx|Delay";
    static constexpr Steinberg::uint32 kProcessorUid[4] = { 0xD3C43EC3, 0x5EB64BA8, 0xBD390A3C, 0x15A35435 };
    static constexpr Steinberg::uint32 kControllerUid[4] = { 0x19F52B74, 0xF8294B81, 0x96382AAC, 0x0579DD00 };

    static std::vector<const char*> choiceLabels(int index) {
        std::vector<const char*> labels;
        if (index == kDelayDivisionL || index == kDelayDivisionR) for (int i = 0; i < kDelayNumDivisions; ++i) labels.push_back(kDelayDivisionLabels[i]);
        return labels;
    }

    static void afterProcess(Core& core, cuif_spsc_ring_buffer& dspToUi) {
        float frame[2] = { core.meterInputDb(), core.meterOutputDb() };
        loudio::vst3::pushFrame(dspToUi, 'M', frame);
    }

#ifdef LOUDIO_VST3_EDITOR
    using Ui = DelayUi;

    static constexpr int kPanelWidth = kDelayPanelWidth;
    static constexpr int kPanelHeight = kDelayPanelHeight;
    static constexpr const char* kWindowTitle = "Loudio Delay";

    static Ui* uiCreate(float w, float h, void* context, loudio::vst3::SendParamsFn sendParams) {
        return delayUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { delayUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { delayUiSyncFromParams(ui, params); }

    static void pollDspToUi(Ui* ui, cuif_spsc_ring_buffer& fromDsp) {
        unsigned char tag;
        float frame[2];
        while (cuif_spsc_readable(&fromDsp) >= sizeof(tag) + sizeof(frame)) {
            if (cuif_spsc_read(&fromDsp, &tag, sizeof(tag)) != sizeof(tag)) break;
            cuif_spsc_read(&fromDsp, (unsigned char*)frame, sizeof(frame));
            if (tag == 'M') delayUiFeedMeters(ui, frame[0], frame[1]);
        }
    }
#endif
};

LOUDIO_VST3_DEFINE_PLUGIN(DelayVst3Traits)
