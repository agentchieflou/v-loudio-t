#include "BusUi.h"

#include <cstdint>
#include <new>

namespace {

BusUi* uiOf(cuif_widget* w) {
    return (w && w->parent) ? static_cast<BusUi*>(w->parent->user_data) : nullptr;
}

void sendOne(BusUi* ui, int index, float value) {
    loudio::ParamUpdate msg = { index, value };
    if (ui->callbacks.sendParams) ui->callbacks.sendParams(ui->callbacks.context, &msg, 1);
}

void knobChanged(cuif_widget* w, float value) {
    if (BusUi* ui = uiOf(w)) {
        int index = (int)(intptr_t)w->user_data;
        sendOne(ui, index, kBusParamTable.fromWidget(index, value));
    }
}

void buttonClicked(cuif_widget* w) {
    if (BusUi* ui = uiOf(w)) sendOne(ui, (int)(intptr_t)w->user_data, w->u.button.state ? 1.0f : 0.0f);
}

void addKnob(BusUi* ui, int index, float x, float y) {
    const loudio::ParamMeta& m = kBusParamTable[index];
    float initial = kBusParamTable.toWidget(index, m.defaultValue);
    cuif_widget* knob = cuif_widget_create_knob(x, y, kBusKnobSize, kBusKnobSize, m.name, initial, knobChanged);
    knob->user_data = (void*)(intptr_t)index;
    ui->knobs[index] = knob;
    cuif_widget_add_child(ui->root, knob);
}

void addButton(BusUi* ui, int index, float x, const char* label) {
    cuif_widget* b = cuif_widget_create_button(x, kBusButtonY, kBusButtonW, kBusButtonH, label, true, buttonClicked);
    b->user_data = (void*)(intptr_t)index;
    ui->buttons[index] = b;
    cuif_widget_add_child(ui->root, b);
}

} // namespace

BusUi* busUiCreate(float w, float h, BusUiCallbacks callbacks) {
    BusUi* ui = new (std::nothrow) BusUi();
    if (!ui) return nullptr;
    ui->callbacks = callbacks;

    ui->root = cuif_widget_create_container(0, 0, w, h);
    ui->root->user_data = ui;

    ui->inMeter = cuif_widget_create_meter(kBusInMeterX, kBusMeterY, kBusMeterW, kBusMeterH, kBusMeterMinDb, 0.0f, true);
    cuif_widget_add_child(ui->root, ui->inMeter);
    ui->outMeter = cuif_widget_create_meter(kBusOutMeterX, kBusMeterY, kBusMeterW, kBusMeterH, kBusMeterMinDb, 0.0f, true);
    cuif_widget_add_child(ui->root, ui->outMeter);

    const int row[] = { kBusGain, kBusPan, kBusWidth };
    for (int i = 0; i < 3; ++i) addKnob(ui, row[i], kBusKnobX0 + (float)i * kBusKnobDx, kBusKnobY);

    const int toggles[] = { kBusPhaseL, kBusPhaseR, kBusSwap, kBusMono, kBusDcBlock };
    const char* labels[] = { "PHASE L", "PHASE R", "SWAP", "MONO", "DC" };
    for (int i = 0; i < 5; ++i) addButton(ui, toggles[i], kBusKnobX0 + (float)i * kBusButtonDx, labels[i]);

    float defaults[kBusNumParams];
    kBusParamTable.fillDefaults(defaults);
    busUiSyncFromParams(ui, defaults);
    busUiFeedMeters(ui, -100.0f, -100.0f);
    return ui;
}

void busUiDestroy(BusUi* ui) {
    if (!ui) return;
    if (ui->root) cuif_widget_destroy(ui->root);
    delete ui;
}

void busUiSyncFromParams(BusUi* ui, const float* params) {
    if (!ui || !params) return;
    for (int i = 0; i < kBusNumParams; ++i) {
        if (ui->knobs[i]) ui->knobs[i]->u.knob.value = kBusParamTable.toWidget(i, params[i]);
        if (ui->buttons[i]) ui->buttons[i]->u.button.state = params[i] > 0.5f;
    }
}

void busUiFeedMeters(BusUi* ui, float inputDb, float outputDb) {
    if (!ui) return;
    cuif_widget_meter_feed(ui->inMeter, inputDb);
    cuif_widget_meter_feed(ui->outMeter, outputDb);
}
