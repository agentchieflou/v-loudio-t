/*
 * VST3 build of the Flanger on the VST3 SDK directly (#171), on the shared
 * shell (VSTs/common/vst3/LoudioVst3.h). No JUCE: FlangerCore does the
 * audio, as in the LPI build (lpi_flanger.cpp); on Windows the editor hosts
 * the plugin's cuif UI in a child window. Everything format-specific lives
 * in the shell; this file is only what makes it the Flanger.
 */
#include "vst3/LoudioVst3.h"
#include "FlangerCore.h"
#ifdef LOUDIO_VST3_EDITOR
#include "FlangerUi.h"
#endif

struct FlangerVst3Traits {
    using Core = FlangerCore;

    static constexpr const char* kName = "Loudio Flanger";
    static constexpr const char* kVendor = "Loudio";
    static constexpr const char* kUrl = "https://github.com/agentchieflou/v-loudio-t";
    static constexpr const char* kEmail = "";
    static constexpr const char* kCategories = "Fx|Modulation";
    static constexpr Steinberg::uint32 kProcessorUid[4] = { 0x23B87C80, 0xC50D4A62, 0x807FE2BB, 0x54D0DB28 };
    static constexpr Steinberg::uint32 kControllerUid[4] = { 0xC11E5134, 0xCD43446E, 0xBB7E5E31, 0xC5AD14A2 };

    static std::vector<const char*> choiceLabels(int) { return {}; }

    static void afterProcess(Core&, cuif_spsc_ring_buffer&) {}

#ifdef LOUDIO_VST3_EDITOR
    using Ui = FlangerUi;

    static constexpr int kPanelWidth = kFlangerPanelWidth;
    static constexpr int kPanelHeight = kFlangerPanelHeight;
    static constexpr const char* kWindowTitle = "Loudio Flanger";

    static Ui* uiCreate(float w, float h, void* context, loudio::vst3::SendParamsFn sendParams) {
        return flangerUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { flangerUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { flangerUiSyncFromParams(ui, params); }

    static void pollDspToUi(Ui*, cuif_spsc_ring_buffer&) {}
#endif
};

LOUDIO_VST3_DEFINE_PLUGIN(FlangerVst3Traits)
