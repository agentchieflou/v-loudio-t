#include "CompressorUi.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace {

/* The root carries the CompressorUi in user_data; each control's
 * user_data is its parameter index. */
CompressorUi* uiOf(cuif_widget* w) {
    return (w && w->parent) ? static_cast<CompressorUi*>(w->parent->user_data) : nullptr;
}

void syncTransfer(CompressorUi* ui) {
    cuif_widget_transfer_curve_set(ui->transfer, ui->params[kCompressorThreshold], ui->params[kCompressorRatio],
                                   ui->params[kCompressorKnee], 0.0f, false);
}

void send(CompressorUi* ui, const loudio::ParamUpdate* msgs, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (kCompressorParamTable.valid(msgs[i].index)) ui->params[msgs[i].index] = msgs[i].value;
    }
    syncTransfer(ui);
    if (ui->callbacks.sendParams) ui->callbacks.sendParams(ui->callbacks.context, msgs, count);
}

void knobChanged(cuif_widget* w, float value) {
    if (CompressorUi* ui = uiOf(w)) {
        int index = (int)(intptr_t)w->user_data;
        loudio::ParamUpdate msg = { index, kCompressorParamTable.fromWidget(index, value) };
        send(ui, &msg, 1);
    }
}

void detectorChanged(cuif_widget* w, float selectedIndex) {
    if (CompressorUi* ui = uiOf(w)) {
        loudio::ParamUpdate msg = { kCompressorDetector, (float)(int)selectedIndex };
        send(ui, &msg, 1);
    }
}

void autoMakeupClicked(cuif_widget* w) {
    if (CompressorUi* ui = uiOf(w)) {
        loudio::ParamUpdate msg = { kCompressorAutoMakeup, w->u.button.state ? 1.0f : 0.0f };
        send(ui, &msg, 1);
    }
}

void addKnob(CompressorUi* ui, int index, float x, float y) {
    const loudio::ParamMeta& m = kCompressorParamTable[index];
    float initial = kCompressorParamTable.toWidget(index, m.defaultValue);
    cuif_widget* knob = cuif_widget_create_knob(x, y, kCompressorKnobSize, kCompressorKnobSize, m.name, initial, knobChanged);
    knob->user_data = (void*)(intptr_t)index;
    ui->knobs[index] = knob;
    cuif_widget_add_child(ui->root, knob);
}

} // namespace

CompressorUi* compressorUiCreate(float w, float h, CompressorUiCallbacks callbacks) {
    CompressorUi* ui = new (std::nothrow) CompressorUi();
    if (!ui) return nullptr;
    ui->callbacks = callbacks;
    kCompressorParamTable.fillDefaults(ui->params);

    ui->root = cuif_widget_create_container(0, 0, w, h);
    ui->root->user_data = ui;

    ui->transfer = cuif_widget_create_transfer_curve(kCompressorCurveX, kCompressorCurveY, kCompressorCurveSize, kCompressorCurveSize,
                                                     kCompressorCurveMinDb, kCompressorCurveMaxDb);
    cuif_widget_add_child(ui->root, ui->transfer);

    ui->inMeter = cuif_widget_create_meter(kCompressorInMeterX, kCompressorMeterY, kCompressorMeterW, kCompressorMeterH, kCompressorCurveMinDb, 0.0f, true);
    cuif_widget_add_child(ui->root, ui->inMeter);
    ui->grMeter = cuif_widget_create_meter(kCompressorGrMeterX, kCompressorMeterY, kCompressorMeterW, kCompressorMeterH, -kCompressorGrMeterRangeDb, 0.0f, true);
    ui->grMeter->u.meter.fill_from_top = true;
    cuif_widget_add_child(ui->root, ui->grMeter);
    ui->outMeter = cuif_widget_create_meter(kCompressorOutMeterX, kCompressorMeterY, kCompressorMeterW, kCompressorMeterH, kCompressorCurveMinDb, 0.0f, true);
    cuif_widget_add_child(ui->root, ui->outMeter);

    const int row1[] = { kCompressorThreshold, kCompressorRatio, kCompressorKnee, kCompressorAttack, kCompressorRelease };
    const int row2[] = { kCompressorLookahead, kCompressorMakeup, kCompressorMix, kCompressorScHpf };
    for (int i = 0; i < 5; ++i) addKnob(ui, row1[i], kCompressorKnobX0 + (float)i * kCompressorKnobDx, kCompressorKnobRow1Y);
    for (int i = 0; i < 4; ++i) addKnob(ui, row2[i], kCompressorKnobX0 + (float)i * kCompressorKnobDx, kCompressorKnobRow2Y);

    ui->detectorDropdown = cuif_widget_create_dropdown(kCompressorDetectorX, kCompressorDetectorY, kCompressorDetectorW, kCompressorDetectorH,
                                                       kCompressorDetectorLabels, kCompressorNumDetectors, detectorChanged);
    ui->detectorDropdown->user_data = (void*)(intptr_t)kCompressorDetector;
    cuif_widget_add_child(ui->root, ui->detectorDropdown);

    ui->autoMakeupButton = cuif_widget_create_button(kCompressorAutoX, kCompressorAutoY, kCompressorAutoW, kCompressorAutoH, "AUTO MAKEUP", true, autoMakeupClicked);
    ui->autoMakeupButton->user_data = (void*)(intptr_t)kCompressorAutoMakeup;
    cuif_widget_add_child(ui->root, ui->autoMakeupButton);

    compressorUiSyncFromParams(ui, ui->params);
    compressorUiFeedMeters(ui, -100.0f, -100.0f, 0.0f);
    return ui;
}

void compressorUiDestroy(CompressorUi* ui) {
    if (!ui) return;
    if (ui->root) cuif_widget_destroy(ui->root);
    delete ui;
}

void compressorUiSyncFromParams(CompressorUi* ui, const float* params) {
    if (!ui || !params) return;
    if (params != ui->params) std::memcpy(ui->params, params, sizeof(ui->params));
    for (int i = 0; i < kCompressorNumParams; ++i) {
        if (ui->knobs[i]) ui->knobs[i]->u.knob.value = kCompressorParamTable.toWidget(i, ui->params[i]);
    }
    int detector = (int)ui->params[kCompressorDetector];
    if (detector < 0) detector = 0;
    if (detector >= kCompressorNumDetectors) detector = kCompressorNumDetectors - 1;
    ui->detectorDropdown->u.dropdown.selected_index = detector;
    ui->autoMakeupButton->u.button.state = ui->params[kCompressorAutoMakeup] > 0.5f;
    syncTransfer(ui);
}

void compressorUiFeedMeters(CompressorUi* ui, float inputDb, float outputDb, float gainReductionDb) {
    if (!ui) return;
    cuif_widget_meter_feed(ui->inMeter, inputDb);
    cuif_widget_meter_feed(ui->outMeter, outputDb);
    cuif_widget_meter_feed(ui->grMeter, -gainReductionDb);
    /* Operating point on the static curve: where the input sits and what
     * the gain computer makes of it (before makeup). */
    cuif_widget_transfer_curve_feed(ui->transfer, inputDb, inputDb - gainReductionDb);
}
