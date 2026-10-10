/*
 * LPI-ABI build of the Limiter on the platform's wrapper template
 * (PluginABI/wrapper/lpi/plugin_template.h). Everything format-specific
 * lives in the template; this file is only what makes it the Limiter.
 * lpi.latency.v1 reports LimiterCore::latencySamples() (lookahead plus
 * the true-peak upsampler's delay).
 */
#include "lpi/plugin_template.h"
#include "LimiterCore.h"
#include "LimiterUi.h"

struct LimiterLpiTraits {
    using Core = LimiterCore;
    using Ui = LimiterUi;

    static constexpr lpi_plugin_info kInfo = { "com.loudio.limiter", "Loudio Limiter", "Loudio", 2, 2 };
    static constexpr int kPanelWidth = kLimiterPanelWidth;
    static constexpr int kPanelHeight = kLimiterPanelHeight;

    static Ui* uiCreate(float w, float h, void* context, loudio::lpi::SendParamsFn sendParams) {
        return limiterUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { limiterUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { limiterUiSyncFromParams(ui, params); }

    static void uiBeforeRender(Ui* ui, Core& core) {
        limiterUiFeedMeters(ui, core.meterInputDb(), core.meterOutputDb(), core.meterGainReductionDb());
    }
};

LPI_DEFINE_PLUGIN(LimiterLpiTraits)
