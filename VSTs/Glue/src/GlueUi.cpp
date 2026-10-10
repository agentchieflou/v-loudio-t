#include "GlueUi.h"

#include <cstdint>
#include <new>

namespace {

GlueUi* uiOf(cuif_widget* w) {
    return (w && w->parent) ? static_cast<GlueUi*>(w->parent->user_data) : nullptr;
}

void sendOne(GlueUi* ui, int index, float value) {
    loudio::ParamUpdate msg = { index, value };
    if (ui->callbacks.sendParams) ui->callbacks.sendParams(ui->callbacks.context, &msg, 1);
}

void knobChanged(cuif_widget* w, float value) {
    if (GlueUi* ui = uiOf(w)) {
        int index = (int)(intptr_t)w->user_data;
        sendOne(ui, index, kGlueParamTable.fromWidget(index, value));
    }
}

/* One callback for the three switches: user_data carries the parameter index. */
void switchChanged(cuif_widget* w, float selectedIndex) {
    if (GlueUi* ui = uiOf(w)) sendOne(ui, (int)(intptr_t)w->user_data, (float)(int)selectedIndex);
}

void addKnob(GlueUi* ui, int index, float x, float y) {
    const loudio::ParamMeta& m = kGlueParamTable[index];
    float initial = kGlueParamTable.toWidget(index, m.defaultValue);
    cuif_widget* knob = cuif_widget_create_knob(x, y, kGlueKnobSize, kGlueKnobSize, m.name, initial, knobChanged);
    knob->user_data = (void*)(intptr_t)index;
    ui->knobs[index] = knob;
    cuif_widget_add_child(ui->root, knob);
}

cuif_widget* addSwitch(GlueUi* ui, int index, float x, const char** labels, int count) {
    cuif_widget* d = cuif_widget_create_dropdown(x, kGlueSwitchY, kGlueSwitchW, kGlueSwitchH, labels, count, switchChanged);
    d->user_data = (void*)(intptr_t)index;
    cuif_widget_add_child(ui->root, d);
    return d;
}

} // namespace

GlueUi* glueUiCreate(float w, float h, GlueUiCallbacks callbacks) {
    GlueUi* ui = new (std::nothrow) GlueUi();
    if (!ui) return nullptr;
    ui->callbacks = callbacks;

    ui->root = cuif_widget_create_container(0, 0, w, h);
    ui->root->user_data = ui;

    ui->inMeter = cuif_widget_create_meter(kGlueInMeterX, kGlueMeterY, kGlueMeterW, kGlueMeterH, kGlueMeterMinDb, 0.0f, true);
    cuif_widget_add_child(ui->root, ui->inMeter);
    ui->grMeter = cuif_widget_create_meter(kGlueGrMeterX, kGlueMeterY, kGlueMeterW, kGlueMeterH, -kGlueGrMeterRangeDb, 0.0f, true);
    ui->grMeter->u.meter.fill_from_top = true;
    cuif_widget_add_child(ui->root, ui->grMeter);
    ui->outMeter = cuif_widget_create_meter(kGlueOutMeterX, kGlueMeterY, kGlueMeterW, kGlueMeterH, kGlueMeterMinDb, 0.0f, true);
    cuif_widget_add_child(ui->root, ui->outMeter);

    const int row[] = { kGlueThreshold, kGlueMakeup, kGlueMix, kGlueScHpf };
    for (int i = 0; i < 4; ++i) addKnob(ui, row[i], kGlueKnobX0 + (float)i * kGlueKnobDx, kGlueKnobY);

    ui->ratioSwitch = addSwitch(ui, kGlueRatio, kGlueKnobX0, kGlueRatioLabels, kGlueNumRatios);
    ui->attackSwitch = addSwitch(ui, kGlueAttack, kGlueKnobX0 + kGlueSwitchDx, kGlueAttackLabels, kGlueNumAttacks);
    ui->releaseSwitch = addSwitch(ui, kGlueRelease, kGlueKnobX0 + 2.0f * kGlueSwitchDx, kGlueReleaseLabels, kGlueNumReleases);

    float defaults[kGlueNumParams];
    kGlueParamTable.fillDefaults(defaults);
    glueUiSyncFromParams(ui, defaults);
    glueUiFeedMeters(ui, -100.0f, -100.0f, 0.0f);
    return ui;
}

void glueUiDestroy(GlueUi* ui) {
    if (!ui) return;
    if (ui->root) cuif_widget_destroy(ui->root);
    delete ui;
}

namespace {
void setSwitch(cuif_widget* d, float value, int count) {
    int i = (int)value;
    if (i < 0) i = 0;
    if (i >= count) i = count - 1;
    d->u.dropdown.selected_index = i;
}
} // namespace

void glueUiSyncFromParams(GlueUi* ui, const float* params) {
    if (!ui || !params) return;
    for (int i = 0; i < kGlueNumParams; ++i) {
        if (ui->knobs[i]) ui->knobs[i]->u.knob.value = kGlueParamTable.toWidget(i, params[i]);
    }
    setSwitch(ui->ratioSwitch, params[kGlueRatio], kGlueNumRatios);
    setSwitch(ui->attackSwitch, params[kGlueAttack], kGlueNumAttacks);
    setSwitch(ui->releaseSwitch, params[kGlueRelease], kGlueNumReleases);
}

void glueUiFeedMeters(GlueUi* ui, float inputDb, float outputDb, float gainReductionDb) {
    if (!ui) return;
    cuif_widget_meter_feed(ui->inMeter, inputDb);
    cuif_widget_meter_feed(ui->outMeter, outputDb);
    cuif_widget_meter_feed(ui->grMeter, -gainReductionDb);
}
