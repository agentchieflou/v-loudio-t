/*
 * LPI-ABI build of the Phaser on the platform's wrapper template
 * (PluginABI/wrapper/lpi/plugin_template.h).
 */
#include "lpi/plugin_template.h"
#include "PhaserCore.h"
#include "PhaserUi.h"

struct PhaserLpiTraits {
    using Core = PhaserCore;
    using Ui = PhaserUi;

    static constexpr lpi_plugin_info kInfo = { "com.loudio.phaser", "Loudio Phaser", "Loudio", 2, 2 };
    static constexpr int kPanelWidth = kPhaserPanelWidth;
    static constexpr int kPanelHeight = kPhaserPanelHeight;

    static Ui* uiCreate(float w, float h, void* context, loudio::lpi::SendParamsFn sendParams) {
        return phaserUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { phaserUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { phaserUiSyncFromParams(ui, params); }
    static void uiBeforeRender(Ui*, Core&) {}
};

LPI_DEFINE_PLUGIN(PhaserLpiTraits)
