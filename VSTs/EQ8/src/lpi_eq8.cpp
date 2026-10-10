/*
 * LPI-ABI build of the EQ-8 on the platform's wrapper template
 * (PluginABI/wrapper/lpi/plugin_template.h). This file is only what makes
 * it the EQ-8.
 */
#include "lpi/plugin_template.h"
#include "EQ8Core.h"
#include "EQ8Ui.h"

struct EQ8LpiTraits {
    using Core = EQ8Core;
    using Ui = EQ8Ui;

    static constexpr lpi_plugin_info kInfo = { "com.loudio.eq8", "Loudio EQ-8", "Loudio", 2, 2 };
    static constexpr int kPanelWidth = kEQ8PanelWidth;
    static constexpr int kPanelHeight = kEQ8PanelHeight;

    static Ui* uiCreate(float w, float h, void* context, loudio::lpi::SendParamsFn sendParams) {
        return eq8UiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { eq8UiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { eq8UiSyncFromParams(ui, params); }

    /* Analyzer magnitudes are read on the GUI thread while the audio
     * thread may be writing them; a torn read only affects the display. */
    static void uiBeforeRender(Ui* ui, Core& core) {
        if (core.isAnalyzerFrameDirty()) core.getAnalyzerMagnitudes(ui->spectrum);
    }
};

LPI_DEFINE_PLUGIN(EQ8LpiTraits)
