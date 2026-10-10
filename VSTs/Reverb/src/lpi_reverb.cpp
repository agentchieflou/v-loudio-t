/*
 * LPI-ABI wrapper for the Reverb (#101/B4). Built directly against
 * ReverbCore -- no JUCE, no APVTS. Implements lpi.gui.offscreen.v1 by
 * building the same widget tree PluginEditor.cpp builds (same
 * ReverbLayout.h rects, same labels), rendered into a cuif offscreen
 * window. PluginEditor.cpp is deliberately left untouched; the widget
 * construction is duplicated here rather than refactored so the shipping
 * VST3 editor can't regress.
 *
 * Threading: audio thread -> process(). One "GUI thread" -> all of the
 * lpi.gui.offscreen.v1 calls AND set_parameter_value (both feed
 * ReverbCore's single-producer queue).
 *
 * Known limitations (v1): one open editor per process (cuif_global_font
 * and the active theme are process globals); analyzer magnitudes are read
 * from the GUI thread while the audio thread may be writing them (torn
 * reads only affect the cosmetic spectrum display).
 */
#include "lpi/lpi.h"
#include "ReverbCore.h"
#include "ReverbLayout.h"
#include "ReverbThemeMapping.h"
#include "cuif/cuif.h"
#include "cuif/cuif_dpi_utils.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace {

constexpr int   kPanelW = 800;
constexpr int   kPanelH = 600;
constexpr float kFontDesignSizePx = 13.0f;

bool g_guiOpen = false; /* v1: one editor per process */

struct ReverbGui {
    ReverbCore* core = nullptr;
    cuif_window* window = nullptr;
    cuif_widget* root = nullptr;
    cuif_widget* mainPage = nullptr;
    cuif_widget* advancedPage = nullptr;
    cuif_widget* knobs[kNumParams] = {};
    cuif_widget* freezeButton = nullptr;
    cuif_widget* modeDropdown = nullptr;
    cuif_widget* themeDropdown = nullptr;
    cuif_widget* bezier = nullptr;
    float leftSpectrum[64] = {};
    float rightSpectrum[64] = {};
    bool ownsFont = false;
    bool buttonDown = false;
    int physW = 0, physH = 0;
};

float widgetToParam(int idx, float w) {
    const ReverbParamMeta& m = kReverbParams[idx];
    if (idx == kParamFreeze) return w > 0.5f ? 1.0f : 0.0f;
    float v = m.minValue + w * (m.maxValue - m.minValue);
    if (idx == kParamMode || idx == kParamUiTheme) v = std::round(v);
    return v;
}

float paramToWidget(int idx, float v) {
    const ReverbParamMeta& m = kReverbParams[idx];
    return (v - m.minValue) / (m.maxValue - m.minValue);
}

ReverbGui* guiOf(cuif_widget* w) {
    return (w && w->parent) ? static_cast<ReverbGui*>(w->parent->user_data) : nullptr;
}

void sendParam(ReverbGui* g, int idx, float value) {
    /* All-or-nothing inside ReverbCore::setParameterValue; a dropped
     * intermediate UI value is fine (see #107). */
    g->core->setParameterValue(idx, value);
}

void knobChanged(cuif_widget* w, float v) {
    if (ReverbGui* g = guiOf(w)) {
        int idx = (int)(intptr_t)w->user_data;
        sendParam(g, idx, widgetToParam(idx, v));
    }
}

void dropdownChanged(cuif_widget* w, float v) {
    if (ReverbGui* g = guiOf(w)) {
        int idx = (int)(intptr_t)w->user_data;
        /* Dropdown reports the selected index directly. */
        sendParam(g, idx, v);
    }
}

void themeChanged(cuif_widget* w, float v) {
    if (ReverbGui* g = guiOf(w)) {
        cuif_set_theme(themeForUiThemeIndex((int)v));
        sendParam(g, kParamUiTheme, (float)(int)v);
    }
}

void buttonClicked(cuif_widget* w) {
    if (ReverbGui* g = guiOf(w)) {
        int idx = (int)(intptr_t)w->user_data;
        sendParam(g, idx, w->u.button.state ? 1.0f : 0.0f);
    }
}

void tabChanged(cuif_widget* w, float v) {
    if (ReverbGui* g = guiOf(w)) {
        bool adv = ((int)v == 1);
        g->mainPage->visible = !adv;
        g->advancedPage->visible = adv;
    }
}

void bezierChanged(cuif_widget* w) {
    ReverbGui* g = static_cast<ReverbGui*>(w->user_data);
    if (!g) return;
    static const int kIdx[3] = { kParamDecayLow, kParamDecayMid, kParamDecayHigh };
    for (int i = 0; i < 3; ++i) {
        sendParam(g, kIdx[i], widgetToParam(kIdx[i], 1.0f - w->u.bezier_editor.node_y[i]));
    }
}

const ReverbWidgetRect& findRect(const std::vector<ReverbWidgetRect>& layout, const char* id) {
    for (const auto& r : layout) {
        if (std::strcmp(r.id, id) == 0) return r;
    }
    static const ReverbWidgetRect kEmpty = { "", ReverbLayoutGroup::Global, 0, 0, 0, 0 };
    return kEmpty;
}

void syncFromCore(ReverbGui* g) {
    for (int i = 0; i < kNumParams; ++i) {
        if (g->knobs[i]) g->knobs[i]->u.knob.value = paramToWidget(i, g->core->getParameterValue(i));
    }
    g->freezeButton->u.button.state = g->core->getParameterValue(kParamFreeze) > 0.5f;
    g->modeDropdown->u.dropdown.selected_index = (int)g->core->getParameterValue(kParamMode);
    int theme = (int)g->core->getParameterValue(kParamUiTheme);
    g->themeDropdown->u.dropdown.selected_index = theme;
    cuif_set_theme(themeForUiThemeIndex(theme));
    for (int i = 0; i < 3; ++i) {
        static const int kIdx[3] = { kParamDecayLow, kParamDecayMid, kParamDecayHigh };
        g->bezier->u.bezier_editor.node_y[i] = 1.0f - paramToWidget(kIdx[i], g->core->getParameterValue(kIdx[i]));
    }
}

void destroyGui(ReverbGui* g) {
    if (!g) return;
    if (g->root) cuif_widget_destroy(g->root);
    if (g->window) cuif_window_destroy(g->window);
    if (g->ownsFont && cuif_global_font) {
        cuif_font_free(cuif_global_font);
        cuif_global_font = nullptr;
    }
    delete g;
    g_guiOpen = false;
}

} // namespace

struct lpi_plugin {
    ReverbCore core;
    double sampleRate = 48000.0;
    uint32_t maxBlock = 512;
    std::vector<float> scratchL, scratchR;
    ReverbGui* gui = nullptr;
};

namespace {

const lpi_plugin_info kInfo = { "com.loudio.reverb", "Loudio Reverb", "Loudio", 2, 2 };

lpi_plugin* apiCreate(double sr, uint32_t maxBlock) {
    lpi_plugin* p = new (std::nothrow) lpi_plugin();
    if (!p) return nullptr;
    p->sampleRate = sr;
    p->maxBlock = maxBlock;
    return p;
}

void apiDestroy(lpi_plugin* p) {
    if (!p) return;
    destroyGui(p->gui);
    delete p;
}

bool apiActivate(lpi_plugin* p) {
    p->scratchL.assign(p->maxBlock, 0.0f);
    p->scratchR.assign(p->maxBlock, 0.0f);
    p->core.prepare(p->sampleRate, (int)p->maxBlock);
    return true;
}

void apiDeactivate(lpi_plugin*) {}

void apiProcess(lpi_plugin* p, const lpi_process_data* d) {
    uint32_t n = d->num_frames;
    if (n > p->maxBlock) n = p->maxBlock; /* contract: never exceeds max_block_size */
    float* l = d->num_output_channels > 0 ? d->outputs[0] : p->scratchL.data();
    float* r = d->num_output_channels > 1 ? d->outputs[1] : p->scratchR.data();
    const float* inL = d->num_input_channels > 0 ? d->inputs[0] : nullptr;
    const float* inR = d->num_input_channels > 1 ? d->inputs[1] : inL;
    for (uint32_t i = 0; i < n; ++i) {
        l[i] = inL ? inL[i] : 0.0f;
        r[i] = inR ? inR[i] : 0.0f;
    }
    p->core.process(l, r, (int)n);
}

uint32_t apiParamCount(lpi_plugin* p) { return (uint32_t)p->core.getParameterCount(); }

bool apiParamInfo(lpi_plugin* p, uint32_t index, lpi_parameter_info* out) {
    ReverbParamMeta m;
    if (!p->core.getParameterInfo((int)index, &m)) return false;
    out->index = index;
    out->id = m.id;
    out->name = m.name;
    out->min_value = m.minValue;
    out->max_value = m.maxValue;
    out->default_value = m.defaultValue;
    out->flags = 0;
    if (m.index == kParamFreeze) out->flags |= LPI_PARAM_BOOLEAN;
    if (m.index == kParamMode || m.index == kParamUiTheme) out->flags |= LPI_PARAM_STEPPED;
    return true;
}

float apiGetParam(lpi_plugin* p, uint32_t index) { return p->core.getParameterValue((int)index); }
bool apiSetParam(lpi_plugin* p, uint32_t index, float v) { return p->core.setParameterValue((int)index, v); }
size_t apiGetState(lpi_plugin* p, void* buf, size_t size) { return p->core.getState(buf, size); }
bool apiSetState(lpi_plugin* p, const void* data, size_t size) { return p->core.setState(data, size); }

/* ---- lpi.gui.offscreen.v1 ---- */

bool guiOpen(lpi_plugin* p, float dpi, lpi_gui_offscreen_info* out) {
    if (!p || p->gui || g_guiOpen) return false;
    if (dpi <= 0.0f) dpi = 1.0f;

    ReverbGui* g = new (std::nothrow) ReverbGui();
    if (!g) return false;
    g->core = &p->core;
    g_guiOpen = true;

    g->window = cuif_window_create_offscreen(kPanelW, kPanelH, dpi);
    if (!g->window) { destroyGui(g); return false; }

    /* Font upload needs the GL context the offscreen window just made current. */
    if (cuif_global_font == nullptr) {
        cuif_global_font = cuif_font_load("C:\\Windows\\Fonts\\arial.ttf", kFontDesignSizePx);
        g->ownsFont = cuif_global_font != nullptr;
        if (cuif_global_font && std::fabs(dpi - 1.0f) > 0.001f) {
            cuif_font_rebake(cuif_global_font, cuif_font_effective_bake_px(kFontDesignSizePx, dpi));
        }
    }

    g->root = cuif_widget_create_container(0, 0, (float)kPanelW, (float)kPanelH);
    g->root->user_data = g;
    auto layout = computeReverbLayout((float)kPanelW, (float)kPanelH);

    g->mainPage = cuif_widget_create_container(0, 0, (float)kPanelW, (float)kPanelH);
    g->mainPage->user_data = g;
    g->mainPage->visible = true;
    cuif_widget_add_child(g->root, g->mainPage);
    g->advancedPage = cuif_widget_create_container(0, 0, (float)kPanelW, (float)kPanelH);
    g->advancedPage->user_data = g;
    g->advancedPage->visible = false;
    cuif_widget_add_child(g->root, g->advancedPage);

    static const char* tabs[] = { "Main", "Advanced" };
    static const char* modes[] = { "Room", "Hall", "Plate", "Cathedral", "Spring" };
    static const char* themes[] = { "Default", "Hello Kitty", "Greens" };
    { const auto& r = findRect(layout, "tabbar");
      cuif_widget_add_child(g->root, cuif_widget_create_tabbar(r.x, r.y, r.w, r.h, tabs, 2, tabChanged)); }
    { const auto& r = findRect(layout, "dropdown.mode");
      g->modeDropdown = cuif_widget_create_dropdown(r.x, r.y, r.w, r.h, modes, 5, dropdownChanged);
      g->modeDropdown->user_data = (void*)(intptr_t)kParamMode;
      cuif_widget_add_child(g->root, g->modeDropdown); }
    { const auto& r = findRect(layout, "dropdown.theme");
      g->themeDropdown = cuif_widget_create_dropdown(r.x, r.y, r.w, r.h, themes, 3, themeChanged);
      g->themeDropdown->user_data = (void*)(intptr_t)kParamUiTheme;
      cuif_widget_add_child(g->root, g->themeDropdown); }
    { const auto& r = findRect(layout, "button.freeze");
      g->freezeButton = cuif_widget_create_button(r.x, r.y, r.w, r.h, "FREEZE", true, buttonClicked);
      g->freezeButton->user_data = (void*)(intptr_t)kParamFreeze;
      cuif_widget_add_child(g->root, g->freezeButton); }

    auto addKnob = [&](cuif_widget* page, int idx, const char* id, const char* label) {
        const auto& r = findRect(layout, id);
        g->knobs[idx] = cuif_widget_create_knob(r.x, r.y, r.w, r.h, label,
                                                paramToWidget(idx, kReverbParams[idx].defaultValue), knobChanged);
        g->knobs[idx]->user_data = (void*)(intptr_t)idx;
        cuif_widget_add_child(page, g->knobs[idx]);
    };
    addKnob(g->mainPage, kParamDecayTime, "knob.decayTime", "Decay Time");
    addKnob(g->mainPage, kParamPreDelay, "knob.preDelay", "Pre-Delay");
    addKnob(g->mainPage, kParamDamping, "knob.damping", "Damping");
    addKnob(g->mainPage, kParamWidth, "knob.width", "Width");
    addKnob(g->mainPage, kParamDryWet, "knob.dryWet", "Dry/Wet");
    addKnob(g->mainPage, kParamDistance, "knob.distance", "Distance");
    addKnob(g->mainPage, kParamThickness, "knob.thickness", "Thickness");

    { const auto& r = findRect(layout, "bezier.decayEq");
      g->bezier = cuif_widget_create_bezier_editor(r.x, r.y, r.w, r.h, 3, bezierChanged);
      g->bezier->user_data = g;
      g->bezier->u.bezier_editor.node_x[0] = 0.1f;
      g->bezier->u.bezier_editor.node_x[1] = 0.5f;
      g->bezier->u.bezier_editor.node_x[2] = 0.9f;
      cuif_widget_add_child(g->mainPage, g->bezier); }
    { const auto& r = findRect(layout, "analyzer.left");
      cuif_widget_add_child(g->mainPage, cuif_widget_create_analyzer(r.x, r.y, r.w, r.h, g->leftSpectrum, 64, cuif_rgba(0.0f, 0.75f, 0.70f, 0.8f))); }
    { const auto& r = findRect(layout, "analyzer.right");
      cuif_widget_add_child(g->mainPage, cuif_widget_create_analyzer(r.x, r.y, r.w, r.h, g->rightSpectrum, 64, cuif_rgba(0.85f, 0.65f, 0.15f, 0.6f))); }

    addKnob(g->advancedPage, kParamPostEqLowGain, "knob.postEqLowGain", "Low EQ");
    addKnob(g->advancedPage, kParamPostEqMidGain, "knob.postEqMidGain", "Mid EQ");
    addKnob(g->advancedPage, kParamPostEqHighGain, "knob.postEqHighGain", "High EQ");
    addKnob(g->advancedPage, kParamCrossoverLowMid, "knob.crossoverLowMid", "Xover Low");
    addKnob(g->advancedPage, kParamCrossoverMidHigh, "knob.crossoverMidHigh", "Xover High");
    addKnob(g->advancedPage, kParamDuckThreshold, "knob.duckThreshold", "Duck Thresh");
    addKnob(g->advancedPage, kParamDuckAmount, "knob.duckAmount", "Duck Depth");
    addKnob(g->advancedPage, kParamDuckRelease, "knob.duckRelease", "Duck Rel");
    addKnob(g->advancedPage, kParamGateThreshold, "knob.gateThreshold", "Gate Thresh");
    addKnob(g->advancedPage, kParamGateTime, "knob.gateTime", "Gate Time");

    cuif_window_set_root_widget(g->window, g->root);
    syncFromCore(g);

    g->physW = cuif_logical_to_physical_px(kPanelW, dpi);
    g->physH = cuif_logical_to_physical_px(kPanelH, dpi);
    p->gui = g;
    if (out) {
        out->logical_width = kPanelW;
        out->logical_height = kPanelH;
        out->physical_width = (uint32_t)g->physW;
        out->physical_height = (uint32_t)g->physH;
    }
    return true;
}

void guiClose(lpi_plugin* p) {
    destroyGui(p->gui);
    p->gui = nullptr;
}

bool guiRender(lpi_plugin* p) {
    ReverbGui* g = p->gui;
    if (!g) return false;
    if (p->core.isLeftAnalyzerFrameDirty()) p->core.getLeftAnalyzerMagnitudes(g->leftSpectrum);
    if (p->core.isRightAnalyzerFrameDirty()) p->core.getRightAnalyzerMagnitudes(g->rightSpectrum);
    cuif_window_render_frame(g->window);
    return true;
}

bool guiReadPixels(lpi_plugin* p, uint8_t* out, size_t size) {
    return p->gui && cuif_window_read_pixels(p->gui->window, out, size);
}

void guiMouse(lpi_plugin* p, int type, float x, float y) {
    ReverbGui* g = p->gui;
    if (!g) return;
    switch (type) {
        case LPI_MOUSE_DOWN: g->buttonDown = true;  cuif_window_inject_mouse_down(g->window, x, y, 0); break;
        case LPI_MOUSE_UP:   g->buttonDown = false; cuif_window_inject_mouse_up(g->window, x, y, 0); break;
        default:             cuif_window_inject_mouse_move(g->window, x, y, g->buttonDown ? 0 : -1); break;
    }
}

const lpi_gui_offscreen_v1 kGuiExt = { guiOpen, guiClose, guiRender, guiReadPixels, guiMouse };

void* apiGetExtension(lpi_plugin*, const char* id) {
    if (id && std::strcmp(id, LPI_EXT_GUI_OFFSCREEN_V1) == 0) return (void*)&kGuiExt;
    return nullptr;
}

const lpi_plugin_api kApi = {
    apiCreate, apiDestroy, apiActivate, apiDeactivate, apiProcess,
    apiParamCount, apiParamInfo, apiGetParam, apiSetParam,
    apiGetState, apiSetState, apiGetExtension
};

const lpi_plugin_info* factoryInfo() { return &kInfo; }
const lpi_plugin_api* factoryApi() { return &kApi; }
const lpi_plugin_factory kFactory = { { LPI_ABI_VERSION_MAJOR, LPI_ABI_VERSION_MINOR }, factoryInfo, factoryApi };

} // namespace

extern "C" LPI_EXPORT const lpi_plugin_factory* lpi_get_factory(void) { return &kFactory; }
