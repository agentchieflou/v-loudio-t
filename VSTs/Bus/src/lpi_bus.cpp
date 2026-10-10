/*
 * LPI-ABI build of the Bus on the platform's wrapper template
 * (PluginABI/wrapper/lpi/plugin_template.h). Everything format-specific
 * lives in the template; this file is only what makes it the Bus.
 */
#include "lpi/plugin_template.h"
#include "BusCore.h"
#include "BusUi.h"

struct BusLpiTraits {
    using Core = BusCore;
    using Ui = BusUi;

    static constexpr lpi_plugin_info kInfo = { "com.loudio.bus", "Loudio Bus", "Loudio", 2, 2 };
    static constexpr int kPanelWidth = kBusPanelWidth;
    static constexpr int kPanelHeight = kBusPanelHeight;

    static Ui* uiCreate(float w, float h, void* context, loudio::lpi::SendParamsFn sendParams) {
        return busUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { busUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { busUiSyncFromParams(ui, params); }
    static void uiBeforeRender(Ui* ui, Core& core) { busUiFeedMeters(ui, core.meterInputDb(), core.meterOutputDb()); }
};

LPI_DEFINE_PLUGIN(BusLpiTraits)
