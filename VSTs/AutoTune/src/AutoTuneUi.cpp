#include "AutoTuneUi.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <new>

namespace {

AutoTuneUi* uiOf(cuif_widget* w) {
    return (w && w->parent) ? static_cast<AutoTuneUi*>(w->parent->user_data) : nullptr;
}

void sendOne(AutoTuneUi* ui, int index, float value) {
    loudio::ParamUpdate msg = { index, value };
    if (ui->callbacks.sendParams) ui->callbacks.sendParams(ui->callbacks.context, &msg, 1);
}

void knobChanged(cuif_widget* w, float value) {
    if (AutoTuneUi* ui = uiOf(w)) {
        int index = (int)(intptr_t)w->user_data;
        sendOne(ui, index, kAutoTuneParamTable.fromWidget(index, value));
    }
}

void dropdownChanged(cuif_widget* w, float selectedIndex) {
    if (AutoTuneUi* ui = uiOf(w)) sendOne(ui, (int)(intptr_t)w->user_data, (float)(int)selectedIndex);
}

void readoutClicked(cuif_widget*) {}

cuif_widget* addDropdown(AutoTuneUi* ui, int index, float x, float w, const char** labels, int count) {
    cuif_widget* d = cuif_widget_create_dropdown(x, kAutoTuneRow1Y, w, kAutoTuneDropH, labels, count, dropdownChanged);
    d->user_data = (void*)(intptr_t)index;
    cuif_widget_add_child(ui->root, d);
    return d;
}

void addKnob(AutoTuneUi* ui, int index, float x, float y) {
    const loudio::ParamMeta& m = kAutoTuneParamTable[index];
    float initial = kAutoTuneParamTable.toWidget(index, m.defaultValue);
    cuif_widget* knob = cuif_widget_create_knob(x, y, kAutoTuneKnobSize, kAutoTuneKnobSize, m.name, initial, knobChanged);
    knob->user_data = (void*)(intptr_t)index;
    ui->knobs[index] = knob;
    cuif_widget_add_child(ui->root, knob);
}

void setDropdown(cuif_widget* d, float value, int count) {
    int i = (int)value;
    if (i < 0) i = 0;
    if (i >= count) i = count - 1;
    d->u.dropdown.selected_index = i;
}

} // namespace

AutoTuneUi* autoTuneUiCreate(float w, float h, AutoTuneUiCallbacks callbacks) {
    AutoTuneUi* ui = new (std::nothrow) AutoTuneUi();
    if (!ui) return nullptr;
    ui->callbacks = callbacks;

    ui->root = cuif_widget_create_container(0, 0, w, h);
    ui->root->user_data = ui;

    ui->keyDropdown = addDropdown(ui, kAutoTuneKey, kAutoTuneKeyX, kAutoTuneKeyW, kAutoTuneKeyLabels, 12);
    ui->scaleDropdown = addDropdown(ui, kAutoTuneScale, kAutoTuneScaleX, kAutoTuneScaleW, kAutoTuneScaleLabels, kAutoTuneNumScales);
    ui->typeDropdown = addDropdown(ui, kAutoTuneInputType, kAutoTuneTypeX, kAutoTuneTypeW, kAutoTuneInputLabels, kAutoTuneNumInputTypes);

    /* The readout is a toggle button the mouse can't reach; its label is the note. */
    ui->noteReadout = cuif_widget_create_button(kAutoTuneNoteX, kAutoTuneNoteY, kAutoTuneNoteW, kAutoTuneNoteH, "--", true, readoutClicked);
    ui->noteReadout->enabled = false;
    cuif_widget_add_child(ui->root, ui->noteReadout);

    const int row[] = { kAutoTuneRetuneSpeed, kAutoTuneHumanise, kAutoTuneFormant, kAutoTuneMix, kAutoTuneReference };
    for (int i = 0; i < 5; ++i) addKnob(ui, row[i], kAutoTuneKnobX0 + (float)i * kAutoTuneKnobDx, kAutoTuneKnobY);

    ui->centsMeter = cuif_widget_create_meter(kAutoTuneMeterX, kAutoTuneMeterY, kAutoTuneMeterW, kAutoTuneMeterH, -kAutoTuneMeterRangeCents, 0.0f, true);
    ui->centsMeter->u.meter.fill_from_top = true;
    cuif_widget_add_child(ui->root, ui->centsMeter);

    float defaults[kAutoTuneNumParams];
    kAutoTuneParamTable.fillDefaults(defaults);
    autoTuneUiSyncFromParams(ui, defaults);
    autoTuneUiFeedPitch(ui, false, 0.0f, 0, 0.0f);
    return ui;
}

void autoTuneUiDestroy(AutoTuneUi* ui) {
    if (!ui) return;
    if (ui->root) cuif_widget_destroy(ui->root);
    delete ui;
}

void autoTuneUiSyncFromParams(AutoTuneUi* ui, const float* params) {
    if (!ui || !params) return;
    for (int i = 0; i < kAutoTuneNumParams; ++i) {
        if (ui->knobs[i]) ui->knobs[i]->u.knob.value = kAutoTuneParamTable.toWidget(i, params[i]);
    }
    setDropdown(ui->keyDropdown, params[kAutoTuneKey], 12);
    setDropdown(ui->scaleDropdown, params[kAutoTuneScale], kAutoTuneNumScales);
    setDropdown(ui->typeDropdown, params[kAutoTuneInputType], kAutoTuneNumInputTypes);
}

void autoTuneUiFeedPitch(AutoTuneUi* ui, bool voiced, float detectedMidi, int targetNote, float correctionCents) {
    if (!ui) return;
    char* label = ui->noteReadout->u.button.label;
    const size_t cap = sizeof(ui->noteReadout->u.button.label);
    if (!voiced) {
        snprintf(label, cap, "--");
        ui->noteReadout->u.button.state = false;
        cuif_widget_meter_feed(ui->centsMeter, 0.0f);
        return;
    }
    const int pc = ((targetNote % 12) + 12) % 12;
    const int octave = targetNote / 12 - 1;
    const int deviation = (int)std::lround((detectedMidi - (float)targetNote) * 100.0f);
    snprintf(label, cap, "%s%d %+dc", kAutoTuneKeyLabels[pc], octave, deviation);
    ui->noteReadout->u.button.state = true;
    cuif_widget_meter_feed(ui->centsMeter, -std::abs(correctionCents));
}
