/*
 * VST3 build of the Phaser on the VST3 SDK directly (#171), on the shared
 * shell (VSTs/common/vst3/LoudioVst3.h). No JUCE: PhaserCore does the
 * audio, as in the LPI build (lpi_phaser.cpp); on Windows the editor hosts
 * the plugin's cuif UI in a child window. Everything format-specific lives
 * in the shell; this file is only what makes it the Phaser.
 */
#include "vst3/LoudioVst3.h"
#include "PhaserCore.h"
#ifdef LOUDIO_VST3_EDITOR
#include "PhaserUi.h"
#endif

struct PhaserVst3Traits {
    using Core = PhaserCore;

    static constexpr const char* kName = "Loudio Phaser";
    static constexpr const char* kVendor = "Loudio";
    static constexpr const char* kUrl = "https://github.com/agentchieflou/v-loudio-t";
    static constexpr const char* kEmail = "";
    static constexpr const char* kCategories = "Fx|Modulation";
    static constexpr Steinberg::uint32 kProcessorUid[4] = { 0x60DAA453, 0x7FA54262, 0x978A5F22, 0xB0157560 };
    static constexpr Steinberg::uint32 kControllerUid[4] = { 0x62634984, 0x2E314E4E, 0xAAD39EFD, 0x34BC2205 };

    static std::vector<const char*> choiceLabels(int index) {
        if (index == kPhaserStages) return { "4", "6", "8", "12" };
        return {};
    }

    static void afterProcess(Core&, cuif_spsc_ring_buffer&) {}

#ifdef LOUDIO_VST3_EDITOR
    using Ui = PhaserUi;

    static constexpr int kPanelWidth = kPhaserPanelWidth;
    static constexpr int kPanelHeight = kPhaserPanelHeight;
    static constexpr const char* kWindowTitle = "Loudio Phaser";

    static Ui* uiCreate(float w, float h, void* context, loudio::vst3::SendParamsFn sendParams) {
        return phaserUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { phaserUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { phaserUiSyncFromParams(ui, params); }

    static void pollDspToUi(Ui*, cuif_spsc_ring_buffer&) {}
#endif
};

LOUDIO_VST3_DEFINE_PLUGIN(PhaserVst3Traits)
