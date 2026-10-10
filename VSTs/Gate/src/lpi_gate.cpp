/*
 * LPI-ABI build of the Gate on the platform's wrapper template
 * (PluginABI/wrapper/lpi/plugin_template.h). Everything format-specific
 * lives in the template; this file is only what makes it the Gate.
 * lpi.latency.v1 reports GateCore::latencySamples() (the lookahead).
 */
#include "lpi/plugin_template.h"
#include "GateCore.h"
#include "GateUi.h"

struct GateLpiTraits {
    using Core = GateCore;
    using Ui = GateUi;

    static constexpr lpi_plugin_info kInfo = { "com.loudio.gate", "Loudio Gate", "Loudio", 2, 2 };
    static constexpr int kPanelWidth = kGatePanelWidth;
    static constexpr int kPanelHeight = kGatePanelHeight;

    static Ui* uiCreate(float w, float h, void* context, loudio::lpi::SendParamsFn sendParams) {
        return gateUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { gateUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { gateUiSyncFromParams(ui, params); }

    /* Meter readings are read on the GUI thread while the audio thread
     * writes them; a torn read only affects the display. */
    static void uiBeforeRender(Ui* ui, Core& core) {
        gateUiFeedMeters(ui, core.meterInputDb(), core.meterGainReductionDb(), core.meterOpen());
    }
};

LPI_DEFINE_PLUGIN(GateLpiTraits)
