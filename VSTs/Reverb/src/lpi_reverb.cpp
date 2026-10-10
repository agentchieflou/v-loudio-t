/*
 * LPI-ABI build of the Reverb (#101/B4), on the platform's wrapper
 * template (PluginABI/wrapper/lpi/plugin_template.h, #128). No JUCE, no
 * APVTS: ReverbCore does the audio, the shared ReverbUi widget tree (the
 * same one the VST3 editor uses) is rendered into a cuif offscreen window
 * for lpi.gui.offscreen.v1. Everything format-specific -- the vtable,
 * gesture tracking, lpi.params.changes.v1, lpi.latency.v1 -- lives in the
 * template; this file is only what makes it the Reverb.
 */
#include "lpi/plugin_template.h"
#include "ReverbCore.h"
#include "ReverbUi.h"

struct ReverbLpiTraits {
    using Core = ReverbCore;
    using Ui = ReverbUi;

    static constexpr lpi_plugin_info kInfo = { "com.loudio.reverb", "Loudio Reverb", "Loudio", 2, 2 };
    static constexpr int kPanelWidth = 800;
    static constexpr int kPanelHeight = 600;

    static Ui* uiCreate(float w, float h, void* context, loudio::lpi::SendParamsFn sendParams) {
        return reverbUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { reverbUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { reverbUiSyncFromParams(ui, params); }

    /* Analyzer magnitudes are read here on the GUI thread while the audio
     * thread may be writing them; a torn read only affects the display. */
    static void uiBeforeRender(Ui* ui, Core& core) {
        if (core.isLeftAnalyzerFrameDirty()) core.getLeftAnalyzerMagnitudes(ui->leftSpectrum);
        if (core.isRightAnalyzerFrameDirty()) core.getRightAnalyzerMagnitudes(ui->rightSpectrum);
    }
};

/* No lookahead anywhere in ReverbCore's chain (pre-delay is a musical
 * effect, not processing latency), so the template's default of 0 for a
 * core without latencySamples() is the honest answer. */

LPI_DEFINE_PLUGIN(ReverbLpiTraits)
