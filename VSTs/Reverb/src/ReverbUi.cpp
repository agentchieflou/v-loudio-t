#include "ReverbUi.h"
#include "ReverbLayout.h"
#include "ReverbThemeMapping.h"
#include "ReverbUiMapping.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

namespace {

/* Pages and root carry the ReverbUi in user_data, so every widget callback
 * can reach it through w->parent; widgets' own user_data is their
 * ReverbParamIndex. */
ReverbUi* uiOf(cuif_widget* w) {
    return (w && w->parent) ? static_cast<ReverbUi*>(w->parent->user_data) : nullptr;
}

void send(ReverbUi* ui, const ParamUpdateMessage* msgs, size_t count) {
    if (ui->callbacks.sendParams) ui->callbacks.sendParams(ui->callbacks.context, msgs, count);
}

void knobChanged(cuif_widget* w, float value) {
    if (ReverbUi* ui = uiOf(w)) {
        int index = (int)(intptr_t)w->user_data;
        ParamUpdateMessage msg = { index, reverbWidgetToParam(index, value) };
        send(ui, &msg, 1);
    }
}

void modeChanged(cuif_widget* w, float selectedIndex) {
    /* Dropdowns report the selected index, which IS the Mode value. */
    if (ReverbUi* ui = uiOf(w)) {
        ParamUpdateMessage msg = { kParamMode, (float)(int)selectedIndex };
        send(ui, &msg, 1);
    }
}

void themeChanged(cuif_widget* w, float selectedIndex) {
    if (ReverbUi* ui = uiOf(w)) {
        int theme = (int)selectedIndex;
        /* Rendering happens on this (UI) thread, so apply immediately. */
        cuif_set_theme(themeForUiThemeIndex(theme));
        ParamUpdateMessage msg = { kParamUiTheme, (float)theme };
        send(ui, &msg, 1);
    }
}

void freezeClicked(cuif_widget* w) {
    if (ReverbUi* ui = uiOf(w)) {
        ParamUpdateMessage msg = { kParamFreeze, w->u.button.state ? 1.0f : 0.0f };
        send(ui, &msg, 1);
    }
}

void tabChanged(cuif_widget* w, float selectedIndex) {
    if (ReverbUi* ui = uiOf(w)) {
        bool advanced = ((int)selectedIndex == 1);
        ui->mainPage->visible = !advanced;
        ui->advancedPage->visible = advanced;
    }
}

const int kDecayEqParams[3] = { kParamDecayLow, kParamDecayMid, kParamDecayHigh };

void decayEqChanged(cuif_widget* w) {
    ReverbUi* ui = static_cast<ReverbUi*>(w->user_data);
    if (!ui) return;
    ParamUpdateMessage msgs[3];
    for (int i = 0; i < 3; ++i) {
        /* Node y grows downward; higher node = longer decay. */
        msgs[i] = { kDecayEqParams[i], reverbWidgetToParam(kDecayEqParams[i], 1.0f - w->u.bezier_editor.node_y[i]) };
    }
    send(ui, msgs, 3); /* one batch: all three nodes or none */
}

const ReverbWidgetRect& findRect(const std::vector<ReverbWidgetRect>& layout, const char* id) {
    for (const auto& r : layout) {
        if (std::strcmp(r.id, id) == 0) return r;
    }
    static const ReverbWidgetRect kMissing = { "", ReverbLayoutGroup::Global, 0, 0, 0, 0 };
    return kMissing;
}

struct KnobSpec { int param; const char* rectId; const char* label; };

const KnobSpec kMainKnobs[] = {
    { kParamDecayTime, "knob.decayTime", "Decay Time" },
    { kParamPreDelay,  "knob.preDelay",  "Pre-Delay" },
    { kParamDamping,   "knob.damping",   "Damping" },
    { kParamWidth,     "knob.width",     "Width" },
    { kParamDryWet,    "knob.dryWet",    "Dry/Wet" },
    { kParamDistance,  "knob.distance",  "Distance" },
    { kParamThickness, "knob.thickness", "Thickness" },
};

const KnobSpec kAdvancedKnobs[] = {
    { kParamPostEqLowGain,    "knob.postEqLowGain",    "Low EQ" },
    { kParamPostEqMidGain,    "knob.postEqMidGain",    "Mid EQ" },
    { kParamPostEqHighGain,   "knob.postEqHighGain",   "High EQ" },
    { kParamCrossoverLowMid,  "knob.crossoverLowMid",  "Xover Low" },
    { kParamCrossoverMidHigh, "knob.crossoverMidHigh", "Xover High" },
    { kParamDuckThreshold,    "knob.duckThreshold",    "Duck Thresh" },
    { kParamDuckAmount,       "knob.duckAmount",       "Duck Depth" },
    { kParamDuckRelease,      "knob.duckRelease",      "Duck Rel" },
    { kParamGateThreshold,    "knob.gateThreshold",    "Gate Thresh" },
    { kParamGateTime,         "knob.gateTime",         "Gate Time" },
};

} // namespace

ReverbUi* reverbUiCreate(float w, float h, ReverbUiCallbacks callbacks) {
    ReverbUi* ui = new (std::nothrow) ReverbUi();
    if (!ui) return nullptr;
    ui->callbacks = callbacks;
    auto layout = computeReverbLayout(w, h);

    ui->root = cuif_widget_create_container(0, 0, w, h);
    ui->root->user_data = ui;
    ui->mainPage = cuif_widget_create_container(0, 0, w, h);
    ui->mainPage->user_data = ui;
    ui->mainPage->visible = true;
    cuif_widget_add_child(ui->root, ui->mainPage);
    ui->advancedPage = cuif_widget_create_container(0, 0, w, h);
    ui->advancedPage->user_data = ui;
    ui->advancedPage->visible = false;
    cuif_widget_add_child(ui->root, ui->advancedPage);

    /* Global header */
    static const char* kTabs[] = { "Main", "Advanced" };
    static const char* kModes[] = { "Room", "Hall", "Plate", "Cathedral", "Spring" };
    static const char* kThemes[] = { "Default", "Hello Kitty", "Greens" };
    {
        const auto& r = findRect(layout, "tabbar");
        ui->tabBar = cuif_widget_create_tabbar(r.x, r.y, r.w, r.h, kTabs, 2, tabChanged);
        cuif_widget_add_child(ui->root, ui->tabBar);
    }
    {
        const auto& r = findRect(layout, "dropdown.mode");
        ui->modeDropdown = cuif_widget_create_dropdown(r.x, r.y, r.w, r.h, kModes, 5, modeChanged);
        ui->modeDropdown->user_data = (void*)(intptr_t)kParamMode;
        cuif_widget_add_child(ui->root, ui->modeDropdown);
    }
    {
        const auto& r = findRect(layout, "dropdown.theme");
        ui->themeDropdown = cuif_widget_create_dropdown(r.x, r.y, r.w, r.h, kThemes, 3, themeChanged);
        ui->themeDropdown->user_data = (void*)(intptr_t)kParamUiTheme;
        cuif_widget_add_child(ui->root, ui->themeDropdown);
    }
    {
        const auto& r = findRect(layout, "button.freeze");
        ui->freezeButton = cuif_widget_create_button(r.x, r.y, r.w, r.h, "FREEZE", true, freezeClicked);
        ui->freezeButton->user_data = (void*)(intptr_t)kParamFreeze;
        cuif_widget_add_child(ui->root, ui->freezeButton);
    }

    auto addKnob = [&](cuif_widget* page, const KnobSpec& k) {
        const auto& r = findRect(layout, k.rectId);
        float initial = reverbParamToWidget(k.param, kReverbParams[k.param].defaultValue);
        ui->knobs[k.param] = cuif_widget_create_knob(r.x, r.y, r.w, r.h, k.label, initial, knobChanged);
        ui->knobs[k.param]->user_data = (void*)(intptr_t)k.param;
        cuif_widget_add_child(page, ui->knobs[k.param]);
    };

    /* Main tab: hero decay knob, secondary row, decay-EQ curve, analyzers */
    for (const auto& k : kMainKnobs) addKnob(ui->mainPage, k);
    {
        const auto& r = findRect(layout, "bezier.decayEq");
        ui->bezierEditor = cuif_widget_create_bezier_editor(r.x, r.y, r.w, r.h, 3, decayEqChanged);
        ui->bezierEditor->user_data = ui;
        ui->bezierEditor->u.bezier_editor.node_x[0] = 0.1f;
        ui->bezierEditor->u.bezier_editor.node_x[1] = 0.5f;
        ui->bezierEditor->u.bezier_editor.node_x[2] = 0.9f;
        cuif_widget_add_child(ui->mainPage, ui->bezierEditor);
    }
    {
        const auto& r = findRect(layout, "analyzer.left");
        ui->analyzerLeft = cuif_widget_create_analyzer(r.x, r.y, r.w, r.h, ui->leftSpectrum, 64, cuif_rgba(0.0f, 0.75f, 0.70f, 0.8f));
        cuif_widget_add_child(ui->mainPage, ui->analyzerLeft);
    }
    {
        const auto& r = findRect(layout, "analyzer.right");
        ui->analyzerRight = cuif_widget_create_analyzer(r.x, r.y, r.w, r.h, ui->rightSpectrum, 64, cuif_rgba(0.85f, 0.65f, 0.15f, 0.6f));
        cuif_widget_add_child(ui->mainPage, ui->analyzerRight);
    }

    /* Advanced tab: post EQ, crossover, ducking, gate */
    for (const auto& k : kAdvancedKnobs) addKnob(ui->advancedPage, k);

    return ui;
}

void reverbUiDestroy(ReverbUi* ui) {
    if (!ui) return;
    if (ui->root) cuif_widget_destroy(ui->root);
    delete ui;
}

void reverbUiSyncFromParams(ReverbUi* ui, const float* params) {
    if (!ui || !params) return;
    for (int i = 0; i < kNumParams; ++i) {
        if (ui->knobs[i]) ui->knobs[i]->u.knob.value = reverbParamToWidget(i, params[i]);
    }
    ui->freezeButton->u.button.state = params[kParamFreeze] > 0.5f;
    ui->modeDropdown->u.dropdown.selected_index = (int)params[kParamMode];
    int theme = (int)params[kParamUiTheme];
    ui->themeDropdown->u.dropdown.selected_index = theme;
    cuif_set_theme(themeForUiThemeIndex(theme));
    for (int i = 0; i < 3; ++i) {
        ui->bezierEditor->u.bezier_editor.node_y[i] = 1.0f - reverbParamToWidget(kDecayEqParams[i], params[kDecayEqParams[i]]);
    }
}
