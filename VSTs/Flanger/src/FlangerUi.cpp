#include "FlangerUi.h"

#include <cstdint>
#include <new>

namespace {

FlangerUi* uiOf(cuif_widget* w) {
    return (w && w->parent) ? static_cast<FlangerUi*>(w->parent->user_data) : nullptr;
}

void sendOne(FlangerUi* ui, int index, float value) {
    loudio::ParamUpdate msg = { index, value };
    if (ui->callbacks.sendParams) ui->callbacks.sendParams(ui->callbacks.context, &msg, 1);
}

void knobChanged(cuif_widget* w, float value) {
    if (FlangerUi* ui = uiOf(w)) {
        int index = (int)(intptr_t)w->user_data;
        sendOne(ui, index, kFlangerParamTable.fromWidget(index, value));
    }
}

void throughZeroClicked(cuif_widget* w) {
    if (FlangerUi* ui = uiOf(w)) sendOne(ui, kFlangerThroughZero, w->u.button.state ? 1.0f : 0.0f);
}

void addKnob(FlangerUi* ui, int index, float x, float y) {
    const loudio::ParamMeta& m = kFlangerParamTable[index];
    float initial = kFlangerParamTable.toWidget(index, m.defaultValue);
    cuif_widget* knob = cuif_widget_create_knob(x, y, kFlangerKnobSize, kFlangerKnobSize, m.name, initial, knobChanged);
    knob->user_data = (void*)(intptr_t)index;
    ui->knobs[index] = knob;
    cuif_widget_add_child(ui->root, knob);
}

} // namespace

FlangerUi* flangerUiCreate(float w, float h, FlangerUiCallbacks callbacks) {
    FlangerUi* ui = new (std::nothrow) FlangerUi();
    if (!ui) return nullptr;
    ui->callbacks = callbacks;

    ui->root = cuif_widget_create_container(0, 0, w, h);
    ui->root->user_data = ui;

    const int row[] = { kFlangerRate, kFlangerDepth, kFlangerCentre, kFlangerFeedback, kFlangerStereoPhase, kFlangerMix };
    for (int i = 0; i < 6; ++i) addKnob(ui, row[i], kFlangerKnobX0 + (float)i * kFlangerKnobDx, kFlangerKnobY);

    ui->throughZeroButton = cuif_widget_create_button(kFlangerTzX, kFlangerTzY, kFlangerTzW, kFlangerTzH, "THROUGH ZERO", true, throughZeroClicked);
    ui->throughZeroButton->user_data = (void*)(intptr_t)kFlangerThroughZero;
    cuif_widget_add_child(ui->root, ui->throughZeroButton);

    float defaults[kFlangerNumParams];
    kFlangerParamTable.fillDefaults(defaults);
    flangerUiSyncFromParams(ui, defaults);
    return ui;
}

void flangerUiDestroy(FlangerUi* ui) {
    if (!ui) return;
    if (ui->root) cuif_widget_destroy(ui->root);
    delete ui;
}

void flangerUiSyncFromParams(FlangerUi* ui, const float* params) {
    if (!ui || !params) return;
    for (int i = 0; i < kFlangerNumParams; ++i) {
        if (ui->knobs[i]) ui->knobs[i]->u.knob.value = kFlangerParamTable.toWidget(i, params[i]);
    }
    ui->throughZeroButton->u.button.state = params[kFlangerThroughZero] > 0.5f;
}
