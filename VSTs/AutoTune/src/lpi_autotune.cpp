/*
 * LPI-ABI build of the AutoTune on the platform's wrapper template
 * (PluginABI/wrapper/lpi/plugin_template.h). lpi.latency.v1 reports the
 * analysis window (AutoTuneCore::latencySamples()).
 */
#include "lpi/plugin_template.h"
#include "AutoTuneCore.h"
#include "AutoTuneUi.h"

struct AutoTuneLpiTraits {
    using Core = AutoTuneCore;
    using Ui = AutoTuneUi;

    static constexpr lpi_plugin_info kInfo = { "com.loudio.autotune", "Loudio AutoTune", "Loudio", 2, 2 };
    static constexpr int kPanelWidth = kAutoTunePanelWidth;
    static constexpr int kPanelHeight = kAutoTunePanelHeight;

    static Ui* uiCreate(float w, float h, void* context, loudio::lpi::SendParamsFn sendParams) {
        return autoTuneUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { autoTuneUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { autoTuneUiSyncFromParams(ui, params); }

    /* The analysis frame is read on the GUI thread while the audio thread
     * writes it; a torn read only affects the display. */
    static void uiBeforeRender(Ui* ui, Core& core) {
        autoTuneUiFeedPitch(ui, core.detectedVoiced(), core.detectedMidi(), core.targetMidi(), core.correctionCents());
    }
};

LPI_DEFINE_PLUGIN(AutoTuneLpiTraits)
