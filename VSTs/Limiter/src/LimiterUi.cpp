#include "LimiterUi.h"

#include <cstdint>
#include <new>

namespace {

LimiterUi* uiOf(cuif_widget* w) {
    return (w && w->parent) ? static_cast<LimiterUi*>(w->parent->user_data) : nullptr;
}

void knobChanged(cuif_widget* w, float value) {
    if (LimiterUi* ui = uiOf(w)) {
        int index = (int)(intptr_t)w->user_data;
        loudio::ParamUpdate msg = { index, kLimiterParamTable.fromWidget(index, value) };
        if (ui->callbacks.sendParams) ui->callbacks.sendParams(ui->callbacks.context, &msg, 1);
    }
}

void osChanged(cuif_widget* w, float selectedIndex) {
    if (LimiterUi* ui = uiOf(w)) {
        loudio::ParamUpdate msg = { kLimiterOversampling, (float)(int)selectedIndex };
        if (ui->callbacks.sendParams) ui->callbacks.sendParams(ui->callbacks.context, &msg, 1);
    }
}

void addKnob(LimiterUi* ui, int index, float x, float y) {
    const loudio::ParamMeta& m = kLimiterParamTable[index];
    float initial = kLimiterParamTable.toWidget(index, m.defaultValue);
    cuif_widget* knob = cuif_widget_create_knob(x, y, kLimiterKnobSize, kLimiterKnobSize, m.name, initial, knobChanged);
    knob->user_data = (void*)(intptr_t)index;
    ui->knobs[index] = knob;
    cuif_widget_add_child(ui->root, knob);
}

} // namespace

LimiterUi* limiterUiCreate(float w, float h, LimiterUiCallbacks callbacks) {
    LimiterUi* ui = new (std::nothrow) LimiterUi();
    if (!ui) return nullptr;
    ui->callbacks = callbacks;

    ui->root = cuif_widget_create_container(0, 0, w, h);
    ui->root->user_data = ui;

    ui->inMeter = cuif_widget_create_meter(kLimiterInMeterX, kLimiterMeterY, kLimiterMeterW, kLimiterMeterH, kLimiterMeterMinDb, 0.0f, true);
    cuif_widget_add_child(ui->root, ui->inMeter);
    ui->grMeter = cuif_widget_create_meter(kLimiterGrMeterX, kLimiterMeterY, kLimiterMeterW, kLimiterMeterH, -kLimiterGrMeterRangeDb, 0.0f, true);
    ui->grMeter->u.meter.fill_from_top = true;
    cuif_widget_add_child(ui->root, ui->grMeter);
    ui->outMeter = cuif_widget_create_meter(kLimiterOutMeterX, kLimiterMeterY, kLimiterMeterW, kLimiterMeterH, kLimiterMeterMinDb, 0.0f, true);
    cuif_widget_add_child(ui->root, ui->outMeter);

    const int row[] = { kLimiterCeiling, kLimiterRelease, kLimiterLookahead, kLimiterInputGain, kLimiterLink };
    for (int i = 0; i < 5; ++i) addKnob(ui, row[i], kLimiterKnobX0 + (float)i * kLimiterKnobDx, kLimiterKnobY);

    ui->osDropdown = cuif_widget_create_dropdown(kLimiterOsX, kLimiterOsY, kLimiterOsW, kLimiterOsH, kLimiterOsLabels, kLimiterNumOs, osChanged);
    ui->osDropdown->user_data = (void*)(intptr_t)kLimiterOversampling;
    cuif_widget_add_child(ui->root, ui->osDropdown);

    limiterUiFeedMeters(ui, -100.0f, -100.0f, 0.0f);
    return ui;
}

void limiterUiDestroy(LimiterUi* ui) {
    if (!ui) return;
    if (ui->root) cuif_widget_destroy(ui->root);
    delete ui;
}

void limiterUiSyncFromParams(LimiterUi* ui, const float* params) {
    if (!ui || !params) return;
    for (int i = 0; i < kLimiterNumParams; ++i) {
        if (ui->knobs[i]) ui->knobs[i]->u.knob.value = kLimiterParamTable.toWidget(i, params[i]);
    }
    int os = (int)params[kLimiterOversampling];
    if (os < 0) os = 0;
    if (os >= kLimiterNumOs) os = kLimiterNumOs - 1;
    ui->osDropdown->u.dropdown.selected_index = os;
}

void limiterUiFeedMeters(LimiterUi* ui, float inputDb, float outputDb, float gainReductionDb) {
    if (!ui) return;
    cuif_widget_meter_feed(ui->inMeter, inputDb);
    cuif_widget_meter_feed(ui->outMeter, outputDb);
    cuif_widget_meter_feed(ui->grMeter, -gainReductionDb);
}
