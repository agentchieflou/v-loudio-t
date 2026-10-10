#include "EQ3Ui.h"

#include <cstdint>
#include <new>

namespace {

const int kGainIndex[3] = { kEQ3LowGain, kEQ3MidGain, kEQ3HighGain };
const int kKillIndex[3] = { kEQ3KillLow, kEQ3KillMid, kEQ3KillHigh };
const char* kKillLabels[3] = { "KILL", "KILL", "KILL" };

EQ3Ui* uiOf(cuif_widget* w) {
    return (w && w->parent) ? static_cast<EQ3Ui*>(w->parent->user_data) : nullptr;
}

void sendOne(EQ3Ui* ui, int index, float value) {
    loudio::ParamUpdate msg = { index, value };
    if (ui->callbacks.sendParams) ui->callbacks.sendParams(ui->callbacks.context, &msg, 1);
}

/* Faders and knobs both carry their parameter index in user_data and
 * map 0..1 through the table. */
void controlChanged(cuif_widget* w, float value) {
    if (EQ3Ui* ui = uiOf(w)) {
        int index = (int)(intptr_t)w->user_data;
        sendOne(ui, index, kEQ3ParamTable.fromWidget(index, value));
    }
}

void killClicked(cuif_widget* w) {
    if (EQ3Ui* ui = uiOf(w)) sendOne(ui, (int)(intptr_t)w->user_data, w->u.button.state ? 1.0f : 0.0f);
}

} // namespace

EQ3Ui* eq3UiCreate(float w, float h, EQ3UiCallbacks callbacks) {
    EQ3Ui* ui = new (std::nothrow) EQ3Ui();
    if (!ui) return nullptr;
    ui->callbacks = callbacks;

    ui->root = cuif_widget_create_container(0, 0, w, h);
    ui->root->user_data = ui;

    for (int b = 0; b < 3; ++b) {
        float x = kEQ3FaderX0 + (float)b * kEQ3FaderDx;
        ui->faders[b] = cuif_widget_create_slider(x, kEQ3FaderY, kEQ3FaderW, kEQ3FaderH, true, controlChanged);
        ui->faders[b]->user_data = (void*)(intptr_t)kGainIndex[b];
        cuif_widget_add_child(ui->root, ui->faders[b]);
        ui->kills[b] = cuif_widget_create_button(x - 10.0f, kEQ3KillY, kEQ3KillW, kEQ3KillH, kKillLabels[b], true, killClicked);
        ui->kills[b]->user_data = (void*)(intptr_t)kKillIndex[b];
        cuif_widget_add_child(ui->root, ui->kills[b]);
    }

    const int knobs[] = { kEQ3XoverLowMid, kEQ3XoverMidHigh };
    for (int i = 0; i < 2; ++i) {
        const loudio::ParamMeta& m = kEQ3ParamTable[knobs[i]];
        float initial = kEQ3ParamTable.toWidget(knobs[i], m.defaultValue);
        cuif_widget* knob = cuif_widget_create_knob(kEQ3KnobX0 + (float)i * kEQ3KnobDx, kEQ3KnobY, kEQ3KnobSize, kEQ3KnobSize, m.name, initial, controlChanged);
        knob->user_data = (void*)(intptr_t)knobs[i];
        ui->knobs[knobs[i]] = knob;
        cuif_widget_add_child(ui->root, knob);
    }

    float defaults[kEQ3NumParams];
    kEQ3ParamTable.fillDefaults(defaults);
    eq3UiSyncFromParams(ui, defaults);
    return ui;
}

void eq3UiDestroy(EQ3Ui* ui) {
    if (!ui) return;
    if (ui->root) cuif_widget_destroy(ui->root);
    delete ui;
}

void eq3UiSyncFromParams(EQ3Ui* ui, const float* params) {
    if (!ui || !params) return;
    for (int b = 0; b < 3; ++b) {
        ui->faders[b]->u.slider.value = kEQ3ParamTable.toWidget(kGainIndex[b], params[kGainIndex[b]]);
        ui->kills[b]->u.button.state = params[kKillIndex[b]] > 0.5f;
    }
    for (int i = 0; i < kEQ3NumParams; ++i) {
        if (ui->knobs[i]) ui->knobs[i]->u.knob.value = kEQ3ParamTable.toWidget(i, params[i]);
    }
}
