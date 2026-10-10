#include "DelayUi.h"

#include <cstdint>
#include <new>

namespace {

DelayUi* uiOf(cuif_widget* w) {
    return (w && w->parent) ? static_cast<DelayUi*>(w->parent->user_data) : nullptr;
}

void sendOne(DelayUi* ui, int index, float value) {
    loudio::ParamUpdate msg = { index, value };
    if (ui->callbacks.sendParams) ui->callbacks.sendParams(ui->callbacks.context, &msg, 1);
}

void knobChanged(cuif_widget* w, float value) {
    if (DelayUi* ui = uiOf(w)) {
        int index = (int)(intptr_t)w->user_data;
        sendOne(ui, index, kDelayParamTable.fromWidget(index, value));
    }
}

void divisionChanged(cuif_widget* w, float selectedIndex) {
    if (DelayUi* ui = uiOf(w)) sendOne(ui, (int)(intptr_t)w->user_data, (float)(int)selectedIndex);
}

void syncClicked(cuif_widget* w) {
    if (DelayUi* ui = uiOf(w)) sendOne(ui, kDelaySync, w->u.button.state ? 1.0f : 0.0f);
}

void addKnob(DelayUi* ui, int index, float x, float y) {
    const loudio::ParamMeta& m = kDelayParamTable[index];
    float initial = kDelayParamTable.toWidget(index, m.defaultValue);
    cuif_widget* knob = cuif_widget_create_knob(x, y, kDelayKnobSize, kDelayKnobSize, m.name, initial, knobChanged);
    knob->user_data = (void*)(intptr_t)index;
    ui->knobs[index] = knob;
    cuif_widget_add_child(ui->root, knob);
}

} // namespace

DelayUi* delayUiCreate(float w, float h, DelayUiCallbacks callbacks) {
    DelayUi* ui = new (std::nothrow) DelayUi();
    if (!ui) return nullptr;
    ui->callbacks = callbacks;

    ui->root = cuif_widget_create_container(0, 0, w, h);
    ui->root->user_data = ui;

    addKnob(ui, kDelayTimeL, kDelayKnobX0, kDelayRow1Y);
    addKnob(ui, kDelayTimeR, kDelayKnobX0 + kDelayKnobDx, kDelayRow1Y);

    ui->syncButton = cuif_widget_create_button(kDelaySyncX, kDelaySyncY, kDelaySyncW, kDelaySyncH, "SYNC", true, syncClicked);
    ui->syncButton->user_data = (void*)(intptr_t)kDelaySync;
    cuif_widget_add_child(ui->root, ui->syncButton);

    ui->divisionL = cuif_widget_create_dropdown(kDelayDivLX, kDelayDivY, kDelayDivW, kDelayDivH, kDelayDivisionLabels, kDelayNumDivisions, divisionChanged);
    ui->divisionL->user_data = (void*)(intptr_t)kDelayDivisionL;
    cuif_widget_add_child(ui->root, ui->divisionL);
    ui->divisionR = cuif_widget_create_dropdown(kDelayDivRX, kDelayDivY, kDelayDivW, kDelayDivH, kDelayDivisionLabels, kDelayNumDivisions, divisionChanged);
    ui->divisionR->user_data = (void*)(intptr_t)kDelayDivisionR;
    cuif_widget_add_child(ui->root, ui->divisionR);

    const int row2[] = { kDelayFeedback, kDelayCrossFeed, kDelayLowCut, kDelayHighCut, kDelayModRate, kDelayModDepth, kDelayMix };
    for (int i = 0; i < 7; ++i) addKnob(ui, row2[i], kDelayKnobX0 + (float)i * kDelayKnobDx, kDelayRow2Y);

    ui->inMeter = cuif_widget_create_meter(kDelayInMeterX, kDelayMeterY, kDelayMeterW, kDelayMeterH, kDelayMeterMinDb, 0.0f, true);
    cuif_widget_add_child(ui->root, ui->inMeter);
    ui->outMeter = cuif_widget_create_meter(kDelayOutMeterX, kDelayMeterY, kDelayMeterW, kDelayMeterH, kDelayMeterMinDb, 0.0f, true);
    cuif_widget_add_child(ui->root, ui->outMeter);

    float defaults[kDelayNumParams];
    kDelayParamTable.fillDefaults(defaults);
    delayUiSyncFromParams(ui, defaults);
    delayUiFeedMeters(ui, -100.0f, -100.0f);
    return ui;
}

void delayUiDestroy(DelayUi* ui) {
    if (!ui) return;
    if (ui->root) cuif_widget_destroy(ui->root);
    delete ui;
}

namespace {
void setDropdown(cuif_widget* d, float value, int count) {
    int i = (int)value;
    if (i < 0) i = 0;
    if (i >= count) i = count - 1;
    d->u.dropdown.selected_index = i;
}
} // namespace

void delayUiSyncFromParams(DelayUi* ui, const float* params) {
    if (!ui || !params) return;
    for (int i = 0; i < kDelayNumParams; ++i) {
        if (ui->knobs[i]) ui->knobs[i]->u.knob.value = kDelayParamTable.toWidget(i, params[i]);
    }
    ui->syncButton->u.button.state = params[kDelaySync] > 0.5f;
    setDropdown(ui->divisionL, params[kDelayDivisionL], kDelayNumDivisions);
    setDropdown(ui->divisionR, params[kDelayDivisionR], kDelayNumDivisions);
}

void delayUiFeedMeters(DelayUi* ui, float inputDb, float outputDb) {
    if (!ui) return;
    cuif_widget_meter_feed(ui->inMeter, inputDb);
    cuif_widget_meter_feed(ui->outMeter, outputDb);
}
