/*
 * LPI-ABI build of the Flanger on the platform's wrapper template
 * (PluginABI/wrapper/lpi/plugin_template.h).
 */
#include "lpi/plugin_template.h"
#include "FlangerCore.h"
#include "FlangerUi.h"

struct FlangerLpiTraits {
    using Core = FlangerCore;
    using Ui = FlangerUi;

    static constexpr lpi_plugin_info kInfo = { "com.loudio.flanger", "Loudio Flanger", "Loudio", 2, 2 };
    static constexpr int kPanelWidth = kFlangerPanelWidth;
    static constexpr int kPanelHeight = kFlangerPanelHeight;

    static Ui* uiCreate(float w, float h, void* context, loudio::lpi::SendParamsFn sendParams) {
        return flangerUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { flangerUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { flangerUiSyncFromParams(ui, params); }
    static void uiBeforeRender(Ui*, Core&) {}
};

LPI_DEFINE_PLUGIN(FlangerLpiTraits)
