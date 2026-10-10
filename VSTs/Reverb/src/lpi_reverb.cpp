/*
 * LPI-ABI wrapper for the Reverb (#101/B4). Built directly against
 * ReverbCore -- no JUCE, no APVTS. Implements lpi.gui.offscreen.v1 by
 * rendering the shared ReverbUi widget tree (the same one the VST3 editor
 * uses) into a cuif offscreen window.
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
#include "ReverbUi.h"
#include "cuif/cuif.h"
#include "cuif/cuif_dpi_utils.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

constexpr int   kPanelW = 800;
constexpr int   kPanelH = 600;
constexpr float kFontDesignSizePx = 13.0f;

bool g_guiOpen = false; /* v1: one editor per process */

struct ReverbGui {
    ReverbCore* core = nullptr;
    cuif_window* window = nullptr;
    ReverbUi* ui = nullptr;
    bool ownsFont = false;
    bool buttonDown = false;
    int physW = 0, physH = 0;
};

void syncFromCore(ReverbGui* g) {
    float params[kNumParams];
    for (int i = 0; i < kNumParams; ++i) params[i] = g->core->getParameterValue(i);
    reverbUiSyncFromParams(g->ui, params);
}

void destroyGui(ReverbGui* g) {
    if (!g) return;
    if (g->window) cuif_window_set_root_widget(g->window, nullptr);
    reverbUiDestroy(g->ui);
    /* Free the font BEFORE the window: its texture lives in the offscreen
     * window's GL context, which is still current here. After
     * cuif_window_destroy() that context is gone and glDeleteTextures()
     * inside cuif_font_free() would be a no-op (font texture leak, found
     * by the noprod host build of #111). */
    if (g->ownsFont && cuif_global_font) {
        cuif_font_free(cuif_global_font);
        cuif_global_font = nullptr;
    }
    if (g->window) cuif_window_destroy(g->window);
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

    /* lpi.params.changes.v1: editor-originated changes awaiting the host's
     * next get_parameter_changes() drain. GUI thread only. */
    struct PendingChange { bool queued = false; float value = 0.0f; uint32_t flags = 0; };
    PendingChange pendingChanges[kNumParams];
    bool gestureActive = false;
    bool touchedInGesture[kNumParams] = {};
};

namespace {

/* ReverbUi sendParams callback: forward to the core (all-or-nothing; a
 * dropped intermediate UI value is fine, see #107) and, for every update
 * the core accepted, queue an lpi.params.changes.v1 entry. */
void sendParams(void* context, const ParamUpdateMessage* msgs, size_t count) {
    lpi_plugin* p = static_cast<lpi_plugin*>(context);
    if (!p->core.setParameterValues(msgs, count)) return;
    for (size_t i = 0; i < count; ++i) {
        int index = msgs[i].index;
        auto& c = p->pendingChanges[index];
        c.queued = true;
        c.value = msgs[i].value;
        if (p->gestureActive && !p->touchedInGesture[index]) {
            p->touchedInGesture[index] = true;
            c.flags |= LPI_PARAM_CHANGE_GESTURE_BEGIN;
        }
    }
}

void beginGesture(lpi_plugin* p) {
    p->gestureActive = true;
    for (bool& t : p->touchedInGesture) t = false;
}

/* Releases every parameter the gesture touched, at its last value. */
void endGesture(lpi_plugin* p) {
    if (!p->gestureActive) return;
    p->gestureActive = false;
    for (int i = 0; i < kNumParams; ++i) {
        if (!p->touchedInGesture[i]) continue;
        p->touchedInGesture[i] = false;
        p->pendingChanges[i].queued = true;
        p->pendingChanges[i].flags |= LPI_PARAM_CHANGE_GESTURE_END;
    }
}

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

    g->ui = reverbUiCreate((float)kPanelW, (float)kPanelH, { p, sendParams });
    if (!g->ui) { destroyGui(g); return false; }
    cuif_window_set_root_widget(g->window, g->ui->root);
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
    endGesture(p); /* closing mid-drag still releases the gesture */
    destroyGui(p->gui);
    p->gui = nullptr;
}

bool guiRender(lpi_plugin* p) {
    ReverbGui* g = p->gui;
    if (!g) return false;
    if (p->core.isLeftAnalyzerFrameDirty()) p->core.getLeftAnalyzerMagnitudes(g->ui->leftSpectrum);
    if (p->core.isRightAnalyzerFrameDirty()) p->core.getRightAnalyzerMagnitudes(g->ui->rightSpectrum);
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
        case LPI_MOUSE_DOWN:
            g->buttonDown = true;
            beginGesture(p);
            cuif_window_inject_mouse_down(g->window, x, y, 0);
            break;
        case LPI_MOUSE_UP:
            g->buttonDown = false;
            /* Inject first: some widgets (dropdown items) commit on release,
             * and that change belongs to this gesture. */
            cuif_window_inject_mouse_up(g->window, x, y, 0);
            endGesture(p);
            break;
        default:             cuif_window_inject_mouse_move(g->window, x, y, g->buttonDown ? 0 : -1); break;
    }
}

const lpi_gui_offscreen_v1 kGuiExt = { guiOpen, guiClose, guiRender, guiReadPixels, guiMouse };

/* ---- lpi.latency.v1 ---- */

/* No lookahead anywhere in ReverbCore's chain (pre-delay is a musical
 * effect, not processing latency), so the honest answer is 0. */
uint32_t latencyGet(lpi_plugin*) { return 0; }

const lpi_latency_v1 kLatencyExt = { latencyGet };

/* ---- lpi.params.changes.v1 ---- */

uint32_t paramChangesGet(lpi_plugin* p, lpi_param_change* out, uint32_t max) {
    if (!p || !out) return 0;
    uint32_t written = 0;
    for (int i = 0; i < kNumParams && written < max; ++i) {
        auto& c = p->pendingChanges[i];
        if (!c.queued) continue;
        out[written++] = { (uint32_t)i, c.value, c.flags };
        c.queued = false;
        c.flags = 0;
    }
    return written;
}

const lpi_param_changes_v1 kParamChangesExt = { paramChangesGet };

void* apiGetExtension(lpi_plugin*, const char* id) {
    if (!id) return nullptr;
    if (std::strcmp(id, LPI_EXT_GUI_OFFSCREEN_V1) == 0) return (void*)&kGuiExt;
    if (std::strcmp(id, LPI_EXT_LATENCY_V1) == 0) return (void*)&kLatencyExt;
    if (std::strcmp(id, LPI_EXT_PARAM_CHANGES_V1) == 0) return (void*)&kParamChangesExt;
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
