/*
 * VST3 build of the EQ3 on the VST3 SDK directly (#171), on the shared
 * shell (VSTs/common/vst3/LoudioVst3.h). No JUCE: EQ3Core does the
 * audio, as in the LPI build (lpi_eq3.cpp); on Windows the editor hosts
 * the plugin's cuif UI in a child window. Everything format-specific lives
 * in the shell; this file is only what makes it the EQ3.
 */
#include "vst3/LoudioVst3.h"
#include "EQ3Core.h"
#ifdef LOUDIO_VST3_EDITOR
#include "EQ3Ui.h"
#endif

struct EQ3Vst3Traits {
    using Core = EQ3Core;

    static constexpr const char* kName = "Loudio EQ-3";
    static constexpr const char* kVendor = "Loudio";
    static constexpr const char* kUrl = "https://github.com/agentchieflou/v-loudio-t";
    static constexpr const char* kEmail = "";
    static constexpr const char* kCategories = "Fx|EQ";
    static constexpr Steinberg::uint32 kProcessorUid[4] = { 0x6B256484, 0x22354C56, 0x937BCFFE, 0x55DEF063 };
    static constexpr Steinberg::uint32 kControllerUid[4] = { 0x9B682213, 0x7E534F96, 0x8B5C6847, 0x3A68220E };

    static std::vector<const char*> choiceLabels(int) { return {}; }

    static void afterProcess(Core&, cuif_spsc_ring_buffer&) {}

#ifdef LOUDIO_VST3_EDITOR
    using Ui = EQ3Ui;

    static constexpr int kPanelWidth = kEQ3PanelWidth;
    static constexpr int kPanelHeight = kEQ3PanelHeight;
    static constexpr const char* kWindowTitle = "Loudio EQ-3";

    static Ui* uiCreate(float w, float h, void* context, loudio::vst3::SendParamsFn sendParams) {
        return eq3UiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { eq3UiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { eq3UiSyncFromParams(ui, params); }

    static void pollDspToUi(Ui*, cuif_spsc_ring_buffer&) {}
#endif
};

LOUDIO_VST3_DEFINE_PLUGIN(EQ3Vst3Traits)
