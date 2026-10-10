#include "EQ8Ui.h"
#include "EQ8Response.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace {

/* The root carries the EQ8Ui in user_data; band widgets carry their band
 * index, the knob its parameter index. */
EQ8Ui* uiOf(cuif_widget* w) {
    return (w && w->parent) ? static_cast<EQ8Ui*>(w->parent->user_data) : nullptr;
}

void send(EQ8Ui* ui, const loudio::ParamUpdate* msgs, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (kEQ8ParamTable.valid(msgs[i].index)) ui->params[msgs[i].index] = msgs[i].value;
    }
    eq8ComputeResponse(ui->params, ui->sampleRate, kEQ8MinHz, kEQ8MaxHz, ui->response, kEQ8ResponsePoints);
    if (ui->callbacks.sendParams) ui->callbacks.sendParams(ui->callbacks.context, msgs, count);
}

void syncCurveBands(EQ8Ui* ui) {
    for (int b = 0; b < kEQ8Bands; ++b) {
        int type = (int)ui->params[eq8BandParam(b, kEQ8BandType)];
        cuif_widget_eq_curve_set_band(ui->curve, b, ui->params[eq8BandParam(b, kEQ8BandFreq)],
                                      ui->params[eq8BandParam(b, kEQ8BandGain)], ui->params[eq8BandParam(b, kEQ8BandQ)],
                                      type != kEQ8Off);
    }
}

/* A handle moved (drag) or its Q changed (wheel): one batch for the band. */
void bandChanged(cuif_widget* w, float bandValue) {
    EQ8Ui* ui = uiOf(w);
    int b = (int)bandValue;
    if (!ui || b < 0 || b >= kEQ8Bands) return;
    loudio::ParamUpdate msgs[3] = {
        { eq8BandParam(b, kEQ8BandFreq), w->u.eq_curve.band_hz[b] },
        { eq8BandParam(b, kEQ8BandGain), w->u.eq_curve.band_gain_db[b] },
        { eq8BandParam(b, kEQ8BandQ),    w->u.eq_curve.band_q[b] },
    };
    send(ui, msgs, 3);
}

void typeChanged(cuif_widget* w, float selectedIndex) {
    EQ8Ui* ui = uiOf(w);
    if (!ui) return;
    int b = (int)(intptr_t)w->user_data;
    loudio::ParamUpdate msg = { eq8BandParam(b, kEQ8BandType), (float)(int)selectedIndex };
    send(ui, &msg, 1);
    syncCurveBands(ui);
}

void gainChanged(cuif_widget* w, float value) {
    if (EQ8Ui* ui = uiOf(w)) {
        loudio::ParamUpdate msg = { kEQ8OutputGain, kEQ8ParamTable.fromWidget(kEQ8OutputGain, value) };
        send(ui, &msg, 1);
    }
}

void bypassClicked(cuif_widget* w) {
    if (EQ8Ui* ui = uiOf(w)) {
        loudio::ParamUpdate msg = { kEQ8Bypass, w->u.button.state ? 1.0f : 0.0f };
        send(ui, &msg, 1);
    }
}

} // namespace

EQ8Ui* eq8UiCreate(float w, float h, EQ8UiCallbacks callbacks) {
    EQ8Ui* ui = new (std::nothrow) EQ8Ui();
    if (!ui) return nullptr;
    ui->callbacks = callbacks;
    kEQ8ParamTable.fillDefaults(ui->params);

    ui->root = cuif_widget_create_container(0, 0, w, h);
    ui->root->user_data = ui;

    ui->analyzer = cuif_widget_create_analyzer(kEQ8CurveX, kEQ8CurveY, kEQ8CurveW, kEQ8CurveH, ui->spectrum, 64, cuif_rgba(0.0f, 0.75f, 0.70f, 0.8f));
    ui->analyzer->enabled = false; /* never takes the mouse: the curve on top does */
    cuif_widget_add_child(ui->root, ui->analyzer);

    ui->curve = cuif_widget_create_eq_curve(kEQ8CurveX, kEQ8CurveY, kEQ8CurveW, kEQ8CurveH, kEQ8Bands,
                                            kEQ8MinHz, kEQ8MaxHz, kEQ8MinDb, kEQ8MaxDb, bandChanged);
    ui->curve->u.eq_curve.draw_background = false;
    cuif_widget_eq_curve_set_response(ui->curve, ui->response, kEQ8ResponsePoints);
    cuif_widget_add_child(ui->root, ui->curve);

    const float dropW = kEQ8CurveW / (float)kEQ8Bands - 6.0f;
    for (int b = 0; b < kEQ8Bands; ++b) {
        float x = kEQ8CurveX + (float)b * (kEQ8CurveW / (float)kEQ8Bands);
        ui->typeDropdowns[b] = cuif_widget_create_dropdown(x, kEQ8CurveY + kEQ8CurveH + 12.0f, dropW, 28.0f, kEQ8TypeLabels, kEQ8NumTypes, typeChanged);
        ui->typeDropdowns[b]->user_data = (void*)(intptr_t)b;
        cuif_widget_add_child(ui->root, ui->typeDropdowns[b]);
    }

    float initial = kEQ8ParamTable.toWidget(kEQ8OutputGain, kEQ8Params[kEQ8OutputGain].defaultValue);
    ui->gainKnob = cuif_widget_create_knob(kEQ8GainKnobX, kEQ8GainKnobY, kEQ8GainKnobSize, kEQ8GainKnobSize, "Output", initial, gainChanged);
    ui->gainKnob->user_data = (void*)(intptr_t)kEQ8OutputGain;
    cuif_widget_add_child(ui->root, ui->gainKnob);

    ui->bypassButton = cuif_widget_create_button(kEQ8BypassX, kEQ8BypassY, kEQ8BypassW, kEQ8BypassH, "BYPASS", true, bypassClicked);
    ui->bypassButton->user_data = (void*)(intptr_t)kEQ8Bypass;
    cuif_widget_add_child(ui->root, ui->bypassButton);

    eq8UiSyncFromParams(ui, ui->params);
    return ui;
}

void eq8UiDestroy(EQ8Ui* ui) {
    if (!ui) return;
    if (ui->root) cuif_widget_destroy(ui->root);
    delete ui;
}

void eq8UiSyncFromParams(EQ8Ui* ui, const float* params) {
    if (!ui || !params) return;
    if (params != ui->params) std::memcpy(ui->params, params, sizeof(ui->params));
    syncCurveBands(ui);
    for (int b = 0; b < kEQ8Bands; ++b) {
        int type = (int)ui->params[eq8BandParam(b, kEQ8BandType)];
        if (type < 0) type = 0;
        if (type >= kEQ8NumTypes) type = kEQ8NumTypes - 1;
        ui->typeDropdowns[b]->u.dropdown.selected_index = type;
    }
    ui->gainKnob->u.knob.value = kEQ8ParamTable.toWidget(kEQ8OutputGain, ui->params[kEQ8OutputGain]);
    ui->bypassButton->u.button.state = ui->params[kEQ8Bypass] > 0.5f;
    eq8ComputeResponse(ui->params, ui->sampleRate, kEQ8MinHz, kEQ8MaxHz, ui->response, kEQ8ResponsePoints);
}
