/*
 * LPI-ABI build of the Glue on the platform's wrapper template
 * (PluginABI/wrapper/lpi/plugin_template.h). Everything format-specific
 * lives in the template; this file is only what makes it the Glue.
 * lpi.latency.v1 reports GlueCore::latencySamples() (the 4x oversampler's
 * fixed round trip).
 */
#include "lpi/plugin_template.h"
#include "GlueCore.h"
#include "GlueUi.h"

struct GlueLpiTraits {
    using Core = GlueCore;
    using Ui = GlueUi;

    static constexpr lpi_plugin_info kInfo = { "com.loudio.glue", "Loudio Glue", "Loudio", 2, 2 };
    static constexpr int kPanelWidth = kGluePanelWidth;
    static constexpr int kPanelHeight = kGluePanelHeight;

    static Ui* uiCreate(float w, float h, void* context, loudio::lpi::SendParamsFn sendParams) {
        return glueUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { glueUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { glueUiSyncFromParams(ui, params); }

    static void uiBeforeRender(Ui* ui, Core& core) {
        glueUiFeedMeters(ui, core.meterInputDb(), core.meterOutputDb(), core.meterGainReductionDb());
    }
};

LPI_DEFINE_PLUGIN(GlueLpiTraits)
