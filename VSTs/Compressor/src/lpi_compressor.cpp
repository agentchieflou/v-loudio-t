/*
 * LPI-ABI build of the Compressor on the platform's wrapper template
 * (PluginABI/wrapper/lpi/plugin_template.h). Everything format-specific
 * lives in the template; this file is only what makes it the Compressor.
 * lpi.latency.v1 reports CompressorCore::latencySamples() (the lookahead).
 */
#include "lpi/plugin_template.h"
#include "CompressorCore.h"
#include "CompressorUi.h"

struct CompressorLpiTraits {
    using Core = CompressorCore;
    using Ui = CompressorUi;

    static constexpr lpi_plugin_info kInfo = { "com.loudio.compressor", "Loudio Compressor", "Loudio", 2, 2 };
    static constexpr int kPanelWidth = kCompressorPanelWidth;
    static constexpr int kPanelHeight = kCompressorPanelHeight;

    static Ui* uiCreate(float w, float h, void* context, loudio::lpi::SendParamsFn sendParams) {
        return compressorUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { compressorUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { compressorUiSyncFromParams(ui, params); }

    /* Meter readings are read on the GUI thread while the audio thread
     * writes them; a torn read only affects the display. */
    static void uiBeforeRender(Ui* ui, Core& core) {
        compressorUiFeedMeters(ui, core.meterInputDb(), core.meterOutputDb(), core.meterGainReductionDb());
    }
};

LPI_DEFINE_PLUGIN(CompressorLpiTraits)
