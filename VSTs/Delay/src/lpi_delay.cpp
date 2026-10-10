/*
 * LPI-ABI build of the Delay on the platform's wrapper template
 * (PluginABI/wrapper/lpi/plugin_template.h). The core has setTransport(),
 * so the template advertises lpi.transport.v1 for the synced divisions.
 */
#include "lpi/plugin_template.h"
#include "DelayCore.h"
#include "DelayUi.h"

struct DelayLpiTraits {
    using Core = DelayCore;
    using Ui = DelayUi;

    static constexpr lpi_plugin_info kInfo = { "com.loudio.delay", "Loudio Delay", "Loudio", 2, 2 };
    static constexpr int kPanelWidth = kDelayPanelWidth;
    static constexpr int kPanelHeight = kDelayPanelHeight;

    static Ui* uiCreate(float w, float h, void* context, loudio::lpi::SendParamsFn sendParams) {
        return delayUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { delayUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { delayUiSyncFromParams(ui, params); }
    static void uiBeforeRender(Ui* ui, Core& core) { delayUiFeedMeters(ui, core.meterInputDb(), core.meterOutputDb()); }
};

LPI_DEFINE_PLUGIN(DelayLpiTraits)
