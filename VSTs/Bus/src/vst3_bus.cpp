/*
 * VST3 build of the Bus on the VST3 SDK directly (#171), on the shared
 * shell (VSTs/common/vst3/LoudioVst3.h). No JUCE: BusCore does the
 * audio, as in the LPI build (lpi_bus.cpp); on Windows the editor hosts
 * the plugin's cuif UI in a child window. Everything format-specific lives
 * in the shell; this file is only what makes it the Bus.
 */
#include "vst3/LoudioVst3.h"
#include "BusCore.h"
#ifdef LOUDIO_VST3_EDITOR
#include "BusUi.h"
#endif

struct BusVst3Traits {
    using Core = BusCore;

    static constexpr const char* kName = "Loudio Bus";
    static constexpr const char* kVendor = "Loudio";
    static constexpr const char* kUrl = "https://github.com/agentchieflou/v-loudio-t";
    static constexpr const char* kEmail = "";
    static constexpr const char* kCategories = "Fx|Tools";
    static constexpr Steinberg::uint32 kProcessorUid[4] = { 0x712A1731, 0x12804C0A, 0x9B4102BA, 0xF867CCE4 };
    static constexpr Steinberg::uint32 kControllerUid[4] = { 0x4FEA2307, 0x194A4923, 0xACE46A17, 0x5AFB0A89 };

    static std::vector<const char*> choiceLabels(int) { return {}; }

    /* One meter frame per block: in, out (dB). */
    static void afterProcess(Core& core, cuif_spsc_ring_buffer& dspToUi) {
        float frame[2] = { core.meterInputDb(), core.meterOutputDb() };
        loudio::vst3::pushFrame(dspToUi, 'M', frame);
    }

#ifdef LOUDIO_VST3_EDITOR
    using Ui = BusUi;

    static constexpr int kPanelWidth = kBusPanelWidth;
    static constexpr int kPanelHeight = kBusPanelHeight;
    static constexpr const char* kWindowTitle = "Loudio Bus";

    static Ui* uiCreate(float w, float h, void* context, loudio::vst3::SendParamsFn sendParams) {
        return busUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { busUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { busUiSyncFromParams(ui, params); }

    static void pollDspToUi(Ui* ui, cuif_spsc_ring_buffer& fromDsp) {
        unsigned char tag;
        float frame[2];
        while (cuif_spsc_readable(&fromDsp) >= sizeof(tag) + sizeof(frame)) {
            if (cuif_spsc_read(&fromDsp, &tag, sizeof(tag)) != sizeof(tag)) break;
            cuif_spsc_read(&fromDsp, (unsigned char*)frame, sizeof(frame));
            if (tag == 'M') busUiFeedMeters(ui, frame[0], frame[1]);
        }
    }
#endif
};

LOUDIO_VST3_DEFINE_PLUGIN(BusVst3Traits)
