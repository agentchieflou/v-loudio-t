#include "PhaserUi.h"

#include <cstdint>
#include <new>

namespace {

PhaserUi* uiOf(cuif_widget* w) {
    return (w && w->parent) ? static_cast<PhaserUi*>(w->parent->user_data) : nullptr;
}

void sendOne(PhaserUi* ui, int index, float value) {
    loudio::ParamUpdate msg = { index, value };
    if (ui->callbacks.sendParams) ui->callbacks.sendParams(ui->callbacks.context, &msg, 1);
}

void knobChanged(cuif_widget* w, float value) {
    if (PhaserUi* ui = uiOf(w)) {
        int index = (int)(intptr_t)w->user_data;
        sendOne(ui, index, kPhaserParamTable.fromWidget(index, value));
    }
}

void stagesChanged(cuif_widget* w, float selectedIndex) {
    if (PhaserUi* ui = uiOf(w)) sendOne(ui, kPhaserStages, (float)(int)selectedIndex);
}

void addKnob(PhaserUi* ui, int index, float x, float y) {
    const loudio::ParamMeta& m = kPhaserParamTable[index];
    float initial = kPhaserParamTable.toWidget(index, m.defaultValue);
    cuif_widget* knob = cuif_widget_create_knob(x, y, kPhaserKnobSize, kPhaserKnobSize, m.name, initial, knobChanged);
    knob->user_data = (void*)(intptr_t)index;
    ui->knobs[index] = knob;
    cuif_widget_add_child(ui->root, knob);
}

} // namespace

PhaserUi* phaserUiCreate(float w, float h, PhaserUiCallbacks callbacks) {
    PhaserUi* ui = new (std::nothrow) PhaserUi();
    if (!ui) return nullptr;
    ui->callbacks = callbacks;

    ui->root = cuif_widget_create_container(0, 0, w, h);
    ui->root->user_data = ui;

    const int row[] = { kPhaserRate, kPhaserDepth, kPhaserCentre, kPhaserFeedback, kPhaserStereoPhase, kPhaserMix };
    for (int i = 0; i < 6; ++i) addKnob(ui, row[i], kPhaserKnobX0 + (float)i * kPhaserKnobDx, kPhaserKnobY);

    ui->stagesDropdown = cuif_widget_create_dropdown(kPhaserStagesX, kPhaserStagesY, kPhaserStagesW, kPhaserStagesH, kPhaserStageLabels, kPhaserNumStageOptions, stagesChanged);
    ui->stagesDropdown->user_data = (void*)(intptr_t)kPhaserStages;
    cuif_widget_add_child(ui->root, ui->stagesDropdown);

    float defaults[kPhaserNumParams];
    kPhaserParamTable.fillDefaults(defaults);
    phaserUiSyncFromParams(ui, defaults);
    return ui;
}

void phaserUiDestroy(PhaserUi* ui) {
    if (!ui) return;
    if (ui->root) cuif_widget_destroy(ui->root);
    delete ui;
}

void phaserUiSyncFromParams(PhaserUi* ui, const float* params) {
    if (!ui || !params) return;
    for (int i = 0; i < kPhaserNumParams; ++i) {
        if (ui->knobs[i]) ui->knobs[i]->u.knob.value = kPhaserParamTable.toWidget(i, params[i]);
    }
    int s = (int)params[kPhaserStages];
    if (s < 0) s = 0;
    if (s >= kPhaserNumStageOptions) s = kPhaserNumStageOptions - 1;
    ui->stagesDropdown->u.dropdown.selected_index = s;
}
