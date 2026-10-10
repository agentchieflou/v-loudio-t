/*
 * LPI-ABI build of the EQ-3 on the platform's wrapper template
 * (PluginABI/wrapper/lpi/plugin_template.h).
 */
#include "lpi/plugin_template.h"
#include "EQ3Core.h"
#include "EQ3Ui.h"

struct EQ3LpiTraits {
    using Core = EQ3Core;
    using Ui = EQ3Ui;

    static constexpr lpi_plugin_info kInfo = { "com.loudio.eq3", "Loudio EQ-3", "Loudio", 2, 2 };
    static constexpr int kPanelWidth = kEQ3PanelWidth;
    static constexpr int kPanelHeight = kEQ3PanelHeight;

    static Ui* uiCreate(float w, float h, void* context, loudio::lpi::SendParamsFn sendParams) {
        return eq3UiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { eq3UiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { eq3UiSyncFromParams(ui, params); }
    static void uiBeforeRender(Ui*, Core&) {}
};

LPI_DEFINE_PLUGIN(EQ3LpiTraits)
