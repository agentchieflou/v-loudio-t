#include "ScratchUi.h"

#include <cstdint>
#include <new>

namespace {

/* The root carries the ScratchUi in user_data; each knob's user_data is its
 * parameter index, so one callback serves every knob. */
ScratchUi* uiOf(cuif_widget* w) {
    return (w && w->parent) ? static_cast<ScratchUi*>(w->parent->user_data) : nullptr;
}

void knobChanged(cuif_widget* w, float value) {
    if (ScratchUi* ui = uiOf(w)) {
        int index = (int)(intptr_t)w->user_data;
        loudio::ParamUpdate msg = { index, kScratchParamTable.fromWidget(index, value) };
        if (ui->callbacks.sendParams) ui->callbacks.sendParams(ui->callbacks.context, &msg, 1);
    }
}

} // namespace

ScratchUi* scratchUiCreate(float w, float h, ScratchUiCallbacks callbacks) {
    ScratchUi* ui = new (std::nothrow) ScratchUi();
    if (!ui) return nullptr;
    ui->callbacks = callbacks;

    ui->root = cuif_widget_create_container(0, 0, w, h);
    ui->root->user_data = ui;

    const loudio::ParamMeta& gain = kScratchParamTable[kScratchGain];
    float initial = kScratchParamTable.toWidget(kScratchGain, gain.defaultValue);
    cuif_widget* knob = cuif_widget_create_knob(kScratchGainKnobX, kScratchGainKnobY, kScratchGainKnobSize, kScratchGainKnobSize,
                                                gain.name, initial, knobChanged);
    knob->user_data = (void*)(intptr_t)kScratchGain;
    ui->knobs[kScratchGain] = knob;
    cuif_widget_add_child(ui->root, knob);

    return ui;
}

void scratchUiDestroy(ScratchUi* ui) {
    if (!ui) return;
    if (ui->root) cuif_widget_destroy(ui->root);
    delete ui;
}

void scratchUiSyncFromParams(ScratchUi* ui, const float* params) {
    if (!ui || !params) return;
    for (int i = 0; i < kScratchNumParams; ++i) {
        if (ui->knobs[i]) ui->knobs[i]->u.knob.value = kScratchParamTable.toWidget(i, params[i]);
    }
}
