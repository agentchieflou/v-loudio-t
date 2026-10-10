#include "GateUi.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace {

GateUi* uiOf(cuif_widget* w) {
    return (w && w->parent) ? static_cast<GateUi*>(w->parent->user_data) : nullptr;
}

void knobChanged(cuif_widget* w, float value) {
    if (GateUi* ui = uiOf(w)) {
        int index = (int)(intptr_t)w->user_data;
        loudio::ParamUpdate msg = { index, kGateParamTable.fromWidget(index, value) };
        if (ui->callbacks.sendParams) ui->callbacks.sendParams(ui->callbacks.context, &msg, 1);
    }
}

void lampClicked(cuif_widget*) {}

void addKnob(GateUi* ui, int index, float x, float y) {
    const loudio::ParamMeta& m = kGateParamTable[index];
    float initial = kGateParamTable.toWidget(index, m.defaultValue);
    cuif_widget* knob = cuif_widget_create_knob(x, y, kGateKnobSize, kGateKnobSize, m.name, initial, knobChanged);
    knob->user_data = (void*)(intptr_t)index;
    ui->knobs[index] = knob;
    cuif_widget_add_child(ui->root, knob);
}

} // namespace

GateUi* gateUiCreate(float w, float h, GateUiCallbacks callbacks) {
    GateUi* ui = new (std::nothrow) GateUi();
    if (!ui) return nullptr;
    ui->callbacks = callbacks;

    ui->root = cuif_widget_create_container(0, 0, w, h);
    ui->root->user_data = ui;

    ui->inMeter = cuif_widget_create_meter(kGateInMeterX, kGateMeterY, kGateMeterW, kGateMeterH, kGateMeterMinDb, 0.0f, true);
    cuif_widget_add_child(ui->root, ui->inMeter);
    ui->grMeter = cuif_widget_create_meter(kGateGrMeterX, kGateMeterY, kGateMeterW, kGateMeterH, -kGateGrMeterRangeDb, 0.0f, true);
    ui->grMeter->u.meter.fill_from_top = true;
    cuif_widget_add_child(ui->root, ui->grMeter);

    /* The lamp is a toggle button the mouse can't reach (enabled = false
     * skips hit-testing but still draws); its state mirrors the gate. */
    ui->lamp = cuif_widget_create_button(kGateLampX, kGateLampY, kGateLampW, kGateLampH, "OPEN", true, lampClicked);
    ui->lamp->enabled = false;
    cuif_widget_add_child(ui->root, ui->lamp);

    const int row1[] = { kGateThreshold, kGateHysteresis, kGateAttack, kGateHold };
    const int row2[] = { kGateRelease, kGateRange, kGateScHpf, kGateLookahead };
    for (int i = 0; i < 4; ++i) addKnob(ui, row1[i], kGateKnobX0 + (float)i * kGateKnobDx, kGateKnobRow1Y);
    for (int i = 0; i < 4; ++i) addKnob(ui, row2[i], kGateKnobX0 + (float)i * kGateKnobDx, kGateKnobRow2Y);

    gateUiFeedMeters(ui, -100.0f, 0.0f, false);
    return ui;
}

void gateUiDestroy(GateUi* ui) {
    if (!ui) return;
    if (ui->root) cuif_widget_destroy(ui->root);
    delete ui;
}

void gateUiSyncFromParams(GateUi* ui, const float* params) {
    if (!ui || !params) return;
    for (int i = 0; i < kGateNumParams; ++i) {
        if (ui->knobs[i]) ui->knobs[i]->u.knob.value = kGateParamTable.toWidget(i, params[i]);
    }
}

void gateUiFeedMeters(GateUi* ui, float inputDb, float gainReductionDb, bool open) {
    if (!ui) return;
    cuif_widget_meter_feed(ui->inMeter, inputDb);
    cuif_widget_meter_feed(ui->grMeter, -gainReductionDb);
    ui->lamp->u.button.state = open;
}
