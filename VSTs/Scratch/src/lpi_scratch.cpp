/*
 * LPI-ABI build of Scratch on the platform's wrapper template
 * (PluginABI/wrapper/lpi/plugin_template.h). Everything format-specific
 * lives in the template; this file is only what makes it Scratch.
 */
#include "lpi/plugin_template.h"
#include "ScratchCore.h"
#include "ScratchUi.h"

struct ScratchLpiTraits {
    using Core = ScratchCore;
    using Ui = ScratchUi;

    static constexpr lpi_plugin_info kInfo = { "com.loudio.scratch", "Loudio Scratch", "Loudio", 2, 2 };
    static constexpr int kPanelWidth = kScratchPanelWidth;
    static constexpr int kPanelHeight = kScratchPanelHeight;

    static Ui* uiCreate(float w, float h, void* context, loudio::lpi::SendParamsFn sendParams) {
        return scratchUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { scratchUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { scratchUiSyncFromParams(ui, params); }
    static void uiBeforeRender(Ui*, Core&) {}
};

LPI_DEFINE_PLUGIN(ScratchLpiTraits)
