/*
 * VST3 build of the Gate on the VST3 SDK directly (#171), on the shared
 * shell (VSTs/common/vst3/LoudioVst3.h). No JUCE: GateCore does the
 * audio, as in the LPI build (lpi_gate.cpp); on Windows the editor hosts
 * the plugin's cuif UI in a child window. Everything format-specific lives
 * in the shell; this file is only what makes it the Gate.
 */
#include "vst3/LoudioVst3.h"
#include "GateCore.h"
#ifdef LOUDIO_VST3_EDITOR
#include "GateUi.h"
#endif

struct GateVst3Traits {
    using Core = GateCore;

    static constexpr const char* kName = "Loudio Gate";
    static constexpr const char* kVendor = "Loudio";
    static constexpr const char* kUrl = "https://github.com/agentchieflou/v-loudio-t";
    static constexpr const char* kEmail = "";
    static constexpr const char* kCategories = "Fx|Dynamics";
    static constexpr Steinberg::uint32 kProcessorUid[4] = { 0xCD7E1665, 0x9C474606, 0xB16F52B5, 0xCAB7DAFB };
    static constexpr Steinberg::uint32 kControllerUid[4] = { 0x15BA103D, 0x16D84640, 0x8ED78CC5, 0x68F29F07 };

    static std::vector<const char*> choiceLabels(int) { return {}; }

    /* One meter frame per block: input dB, gain reduction dB, open (1/0). */
    static void afterProcess(Core& core, cuif_spsc_ring_buffer& dspToUi) {
        float frame[3] = { core.meterInputDb(), core.meterGainReductionDb(), core.meterOpen() ? 1.0f : 0.0f };
        loudio::vst3::pushFrame(dspToUi, 'M', frame);
    }

#ifdef LOUDIO_VST3_EDITOR
    using Ui = GateUi;

    static constexpr int kPanelWidth = kGatePanelWidth;
    static constexpr int kPanelHeight = kGatePanelHeight;
    static constexpr const char* kWindowTitle = "Loudio Gate";

    static Ui* uiCreate(float w, float h, void* context, loudio::vst3::SendParamsFn sendParams) {
        return gateUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { gateUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { gateUiSyncFromParams(ui, params); }

    static void pollDspToUi(Ui* ui, cuif_spsc_ring_buffer& fromDsp) {
        unsigned char tag;
        float frame[3];
        while (cuif_spsc_readable(&fromDsp) >= sizeof(tag) + sizeof(frame)) {
            if (cuif_spsc_read(&fromDsp, &tag, sizeof(tag)) != sizeof(tag)) break;
            cuif_spsc_read(&fromDsp, (unsigned char*)frame, sizeof(frame));
            if (tag == 'M') gateUiFeedMeters(ui, frame[0], frame[1], frame[2] > 0.5f);
        }
    }
#endif
};

LOUDIO_VST3_DEFINE_PLUGIN(GateVst3Traits)
