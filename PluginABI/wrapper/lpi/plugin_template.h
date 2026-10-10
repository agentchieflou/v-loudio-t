#pragma once

/*
 * LPI plugin wrapper template (P2 of the plugin platform, #128).
 *
 * Given a Traits type that names a Core (DSP/include/loudio/plugin/
 * CORE_CONTRACT.md) and a cuif UI builder with the ReverbUi-style
 * sendParams callback contract, this header emits the whole plugin:
 * the lpi_plugin_api vtable, lpi.gui.offscreen.v1 (offscreen window, font
 * lifetime, mouse injection, gesture tracking), lpi.latency.v1 and
 * lpi.params.changes.v1 (coalesced queue, GESTURE_BEGIN/END, dropped on
 * deactivate). A plugin's lpi_<name>.cpp is reduced to its Traits plus one
 * LPI_DEFINE_PLUGIN(Traits) line -- see VSTs/Reverb/src/lpi_reverb.cpp.
 *
 * Traits contract:
 *
 *   struct MyTraits {
 *       using Core = MyCore;                         // the P1 core contract
 *       using Ui   = MyUi;                           // opaque UI handle type
 *       static constexpr lpi_plugin_info kInfo = { "com.vendor.my", "My Plugin", "Vendor", 2, 2 };
 *       static constexpr int kPanelWidth  = 800;     // logical px
 *       static constexpr int kPanelHeight = 600;
 *       static Ui*  uiCreate(float w, float h, void* context, loudio::lpi::SendParamsFn sendParams);
 *       static void uiDestroy(Ui* ui);               // safe on nullptr
 *       static cuif_widget* uiRoot(Ui* ui);
 *       static void uiSyncFromParams(Ui* ui, const float* params);   // never calls sendParams
 *       static void uiBeforeRender(Ui* ui, Core& core);              // e.g. copy analyzer frames; may be empty
 *   };
 *   LPI_DEFINE_PLUGIN(MyTraits)
 *
 * The UI calls sendParams(context, updates, count) for every edit, with
 * related edits batched; the wrapper forwards them to the core
 * (all-or-nothing) and queues lpi.params.changes.v1 entries for the ones
 * the core accepted. The wrapper reports Core::latencySamples() through
 * lpi.latency.v1 when the core has it, 0 otherwise, and advertises
 * lpi.transport.v1 only when the core has
 * setTransport(const loudio::TransportInfo&).
 *
 * Threading: audio thread -> process(). One "GUI thread" -> all of the
 * lpi.gui.offscreen.v1 calls AND set_parameter_value (both feed the core's
 * single-producer queue).
 *
 * Known limitations (v1, inherited from the Reverb wrapper): one open
 * editor per plugin DLL (cuif_global_font and the active theme are
 * globals); stereo in/out only; UI reads the core's analyzer data on the
 * GUI thread while the audio thread may be writing it (torn reads only
 * affect the cosmetic display).
 */

#include "lpi/lpi.h"
#include "loudio/plugin/ParamTable.h"
#include "loudio/plugin/Transport.h"
#include "cuif/cuif.h"
#include "cuif/cuif_dpi_utils.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

static_assert((uint32_t)loudio::kParamBoolean == (uint32_t)LPI_PARAM_BOOLEAN, "loudio::ParamFlags must mirror LPI_PARAM_*");
static_assert((uint32_t)loudio::kParamStepped == (uint32_t)LPI_PARAM_STEPPED, "loudio::ParamFlags must mirror LPI_PARAM_*");
static_assert((uint32_t)loudio::kParamReadOnly == (uint32_t)LPI_PARAM_READONLY, "loudio::ParamFlags must mirror LPI_PARAM_*");
static_assert((uint32_t)loudio::kTransportPlaying == (uint32_t)LPI_TRANSPORT_PLAYING, "loudio::TransportFlags must mirror LPI_TRANSPORT_*");
static_assert((uint32_t)loudio::kTransportLooping == (uint32_t)LPI_TRANSPORT_LOOPING, "loudio::TransportFlags must mirror LPI_TRANSPORT_*");
static_assert((uint32_t)loudio::kTransportTempoValid == (uint32_t)LPI_TRANSPORT_TEMPO_VALID, "loudio::TransportFlags must mirror LPI_TRANSPORT_*");
static_assert((uint32_t)loudio::kTransportPpqValid == (uint32_t)LPI_TRANSPORT_PPQ_VALID, "loudio::TransportFlags must mirror LPI_TRANSPORT_*");

namespace loudio {
namespace lpi {

typedef void (*SendParamsFn)(void* context, const ParamUpdate* updates, size_t count);

constexpr const char* kFontPath = "C:\\Windows\\Fonts\\arial.ttf";
constexpr float kFontDesignSizePx = 13.0f;

/* One open editor per DLL (v1). */
inline bool g_guiOpen = false;

template <class Core>
constexpr bool coreHasTransport() {
    return requires (Core& c, const TransportInfo& t) { c.setTransport(t); };
}

template <class Core>
uint32_t coreLatency(const Core& core) {
    if constexpr (requires { core.latencySamples(); }) {
        return (uint32_t)core.latencySamples();
    } else {
        (void)core;
        return 0;
    }
}

template <class Traits>
struct Gui {
    using Core = typename Traits::Core;
    using Ui = typename Traits::Ui;

    cuif_window* window = nullptr;
    Ui* ui = nullptr;
    bool ownsFont = false;
    bool buttonDown = false;
    int physW = 0, physH = 0;

    static void destroy(Gui* g) {
        if (!g) return;
        if (g->window) cuif_window_set_root_widget(g->window, nullptr);
        Traits::uiDestroy(g->ui);
        /* Free the font BEFORE the window: its texture lives in the
         * offscreen window's GL context, which is still current here.
         * After cuif_window_destroy() that context is gone and
         * glDeleteTextures() inside cuif_font_free() would be a no-op
         * (font texture leak, found by the noprod host build of #111). */
        if (g->ownsFont && cuif_global_font) {
            cuif_font_free(cuif_global_font);
            cuif_global_font = nullptr;
        }
        if (g->window) cuif_window_destroy(g->window);
        delete g;
        g_guiOpen = false;
    }
};

/* Per-instance state. The plugin's `struct lpi_plugin` derives from this
 * (LPI_DEFINE_PLUGIN), which is what lets the vtable functions below take
 * the opaque lpi_plugin* the ABI hands around. */
template <class Traits>
struct Instance {
    using Core = typename Traits::Core;

    Core core;
    double sampleRate = 48000.0;
    uint32_t maxBlock = 512;
    int paramCount = 0;
    std::vector<float> scratchL, scratchR;
    std::vector<float> paramSnapshot; /* GUI thread: values pushed into the UI */
    Gui<Traits>* gui = nullptr;

    /* lpi.params.changes.v1: editor-originated changes awaiting the host's
     * next get_parameter_changes() drain. GUI thread only. */
    struct PendingChange { bool queued = false; float value = 0.0f; uint32_t flags = 0; };
    std::vector<PendingChange> pendingChanges;
    std::vector<unsigned char> touchedInGesture; /* bool per parameter */
    bool gestureActive = false;
};

/* Plugin is the concrete `struct lpi_plugin` the LPI_DEFINE_PLUGIN macro
 * derives from Instance<Traits>. It is a template parameter (rather than
 * named directly) only so its use here is dependent: lpi.h forward-declares
 * lpi_plugin, and the definition comes after this header. */
template <class Traits, class Plugin>
struct Api {
    using Core = typename Traits::Core;
    using Inst = Instance<Traits>;
    using GuiT = Gui<Traits>;

    static Inst* self(lpi_plugin* p) { return static_cast<Plugin*>(p); }

    /* UI sendParams callback: forward to the core (all-or-nothing; a
     * dropped intermediate UI value is fine, see #107) and, for every
     * update the core accepted, queue an lpi.params.changes.v1 entry.
     * Read-only parameters are never queued (lpi.h). */
    static void sendParams(void* context, const ParamUpdate* msgs, size_t count) {
        Inst* p = static_cast<Inst*>(context);
        if (!p->core.setParameterValues(msgs, count)) return;
        for (size_t i = 0; i < count; ++i) {
            int index = msgs[i].index;
            if (Core::table()[index].flags & kParamReadOnly) continue;
            auto& c = p->pendingChanges[(size_t)index];
            c.queued = true;
            c.value = msgs[i].value;
            if (p->gestureActive && !p->touchedInGesture[(size_t)index]) {
                p->touchedInGesture[(size_t)index] = 1;
                c.flags |= LPI_PARAM_CHANGE_GESTURE_BEGIN;
            }
        }
    }

    static void beginGesture(Inst* p) {
        p->gestureActive = true;
        for (auto& t : p->touchedInGesture) t = 0;
    }

    /* Releases every parameter the gesture touched, at its last value. */
    static void endGesture(Inst* p) {
        if (!p->gestureActive) return;
        p->gestureActive = false;
        for (int i = 0; i < p->paramCount; ++i) {
            if (!p->touchedInGesture[(size_t)i]) continue;
            p->touchedInGesture[(size_t)i] = 0;
            p->pendingChanges[(size_t)i].queued = true;
            p->pendingChanges[(size_t)i].flags |= LPI_PARAM_CHANGE_GESTURE_END;
        }
    }

    static void syncFromCore(Inst* p) {
        for (int i = 0; i < p->paramCount; ++i) p->paramSnapshot[(size_t)i] = p->core.getParameterValue(i);
        Traits::uiSyncFromParams(p->gui->ui, p->paramSnapshot.data());
    }

    /* ---- lpi_plugin_api ---- */

    static lpi_plugin* create(double sr, uint32_t maxBlock) {
        Plugin* plugin = new (std::nothrow) Plugin();
        if (!plugin) return nullptr;
        Inst* p = plugin;
        p->sampleRate = sr;
        p->maxBlock = maxBlock;
        p->paramCount = Core::table().count;
        p->paramSnapshot.assign((size_t)p->paramCount, 0.0f);
        p->pendingChanges.assign((size_t)p->paramCount, {});
        p->touchedInGesture.assign((size_t)p->paramCount, 0);
        return plugin;
    }

    static void destroy(lpi_plugin* plugin) {
        if (!plugin) return;
        Inst* p = self(plugin);
        GuiT::destroy(p->gui);
        delete static_cast<Plugin*>(plugin);
    }

    static bool activate(lpi_plugin* plugin) {
        Inst* p = self(plugin);
        p->scratchL.assign(p->maxBlock, 0.0f);
        p->scratchR.assign(p->maxBlock, 0.0f);
        p->core.prepare(p->sampleRate, (int)p->maxBlock);
        return true;
    }

    static void deactivate(lpi_plugin* plugin) {
        Inst* p = self(plugin);
        /* lpi.params.changes.v1 lifetime: the queue doesn't survive deactivation. */
        for (auto& c : p->pendingChanges) c = {};
        p->gestureActive = false;
        for (auto& t : p->touchedInGesture) t = 0;
    }

    static void process(lpi_plugin* plugin, const lpi_process_data* d) {
        Inst* p = self(plugin);
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

    static uint32_t paramCount(lpi_plugin* plugin) { return (uint32_t)self(plugin)->core.getParameterCount(); }

    static bool paramInfo(lpi_plugin* plugin, uint32_t index, lpi_parameter_info* out) {
        ParamMeta m;
        if (!self(plugin)->core.getParameterInfo((int)index, &m)) return false;
        out->index = index;
        out->id = m.id;
        out->name = m.name;
        out->min_value = m.minValue;
        out->max_value = m.maxValue;
        out->default_value = m.defaultValue;
        out->flags = m.flags; /* ParamFlags == LPI_PARAM_* bit for bit (asserted above) */
        return true;
    }

    static float getParam(lpi_plugin* plugin, uint32_t index) { return self(plugin)->core.getParameterValue((int)index); }
    static bool setParam(lpi_plugin* plugin, uint32_t index, float v) { return self(plugin)->core.setParameterValue((int)index, v); }
    static size_t getState(lpi_plugin* plugin, void* buf, size_t size) { return self(plugin)->core.getState(buf, size); }
    static bool setState(lpi_plugin* plugin, const void* data, size_t size) { return self(plugin)->core.setState(data, size); }

    /* ---- lpi.gui.offscreen.v1 ---- */

    static bool guiOpen(lpi_plugin* plugin, float dpi, lpi_gui_offscreen_info* out) {
        if (!plugin) return false;
        Inst* p = self(plugin);
        if (p->gui || g_guiOpen) return false;
        if (dpi <= 0.0f) dpi = 1.0f;

        GuiT* g = new (std::nothrow) GuiT();
        if (!g) return false;
        g_guiOpen = true;

        g->window = cuif_window_create_offscreen(Traits::kPanelWidth, Traits::kPanelHeight, dpi);
        if (!g->window) { GuiT::destroy(g); return false; }

        /* Font upload needs the GL context the offscreen window just made current. */
        if (cuif_global_font == nullptr) {
            cuif_global_font = cuif_font_load(kFontPath, kFontDesignSizePx);
            g->ownsFont = cuif_global_font != nullptr;
            if (cuif_global_font && std::fabs(dpi - 1.0f) > 0.001f) {
                cuif_font_rebake(cuif_global_font, cuif_font_effective_bake_px(kFontDesignSizePx, dpi));
            }
        }

        g->ui = Traits::uiCreate((float)Traits::kPanelWidth, (float)Traits::kPanelHeight, p, &Api::sendParams);
        if (!g->ui) { GuiT::destroy(g); return false; }
        cuif_window_set_root_widget(g->window, Traits::uiRoot(g->ui));
        p->gui = g;
        syncFromCore(p);

        g->physW = cuif_logical_to_physical_px(Traits::kPanelWidth, dpi);
        g->physH = cuif_logical_to_physical_px(Traits::kPanelHeight, dpi);
        if (out) {
            out->logical_width = (uint32_t)Traits::kPanelWidth;
            out->logical_height = (uint32_t)Traits::kPanelHeight;
            out->physical_width = (uint32_t)g->physW;
            out->physical_height = (uint32_t)g->physH;
        }
        return true;
    }

    static void guiClose(lpi_plugin* plugin) {
        Inst* p = self(plugin);
        endGesture(p); /* closing mid-drag still releases the gesture */
        GuiT::destroy(p->gui);
        p->gui = nullptr;
    }

    static bool guiRender(lpi_plugin* plugin) {
        Inst* p = self(plugin);
        GuiT* g = p->gui;
        if (!g) return false;
        Traits::uiBeforeRender(g->ui, p->core);
        cuif_window_render_frame(g->window);
        return true;
    }

    static bool guiReadPixels(lpi_plugin* plugin, uint8_t* out, size_t size) {
        Inst* p = self(plugin);
        return p->gui && cuif_window_read_pixels(p->gui->window, out, size);
    }

    static void guiMouse(lpi_plugin* plugin, int type, float x, float y) {
        Inst* p = self(plugin);
        GuiT* g = p->gui;
        if (!g) return;
        switch (type) {
            case LPI_MOUSE_DOWN:
                g->buttonDown = true;
                beginGesture(p);
                cuif_window_inject_mouse_down(g->window, x, y, 0);
                break;
            case LPI_MOUSE_UP:
                g->buttonDown = false;
                /* Inject first: some widgets (dropdown items) commit on
                 * release, and that change belongs to this gesture. */
                cuif_window_inject_mouse_up(g->window, x, y, 0);
                endGesture(p);
                break;
            default:
                cuif_window_inject_mouse_move(g->window, x, y, g->buttonDown ? 0 : -1);
                break;
        }
    }

    /* ---- lpi.latency.v1 ---- */

    static uint32_t latencyGet(lpi_plugin* plugin) { return coreLatency(self(plugin)->core); }

    /* ---- lpi.transport.v1 (only when the core has setTransport) ---- */

    static void transportSet(lpi_plugin* plugin, const lpi_transport_info* info) {
        if (!plugin || !info) return;
        if constexpr (coreHasTransport<Core>()) {
            TransportInfo t;
            t.tempoBpm = info->tempo_bpm;
            t.ppqPosition = info->ppq_position;
            t.sampleRate = info->sample_rate;
            t.flags = info->flags;
            self(plugin)->core.setTransport(t);
        }
    }

    /* ---- lpi.params.changes.v1 ---- */

    static uint32_t paramChangesGet(lpi_plugin* plugin, lpi_param_change* out, uint32_t max) {
        if (!plugin || !out) return 0;
        Inst* p = self(plugin);
        uint32_t written = 0;
        for (int i = 0; i < p->paramCount && written < max; ++i) {
            auto& c = p->pendingChanges[(size_t)i];
            if (!c.queued) continue;
            out[written++] = { (uint32_t)i, c.value, c.flags };
            c.queued = false;
            c.flags = 0;
        }
        return written;
    }

    static void* getExtension(lpi_plugin*, const char* id) {
        if (!id) return nullptr;
        if (std::strcmp(id, LPI_EXT_GUI_OFFSCREEN_V1) == 0) return (void*)&kGuiExt;
        if (std::strcmp(id, LPI_EXT_LATENCY_V1) == 0) return (void*)&kLatencyExt;
        if (std::strcmp(id, LPI_EXT_PARAM_CHANGES_V1) == 0) return (void*)&kParamChangesExt;
        if (std::strcmp(id, LPI_EXT_TRANSPORT_V1) == 0) return coreHasTransport<Core>() ? (void*)&kTransportExt : nullptr;
        return nullptr;
    }

    static const lpi_plugin_info* factoryInfo() { return &Traits::kInfo; }
    static const lpi_plugin_api* factoryApi() { return &kApi; }
    static const lpi_plugin_factory* factory() { return &kFactory; }

    static inline const lpi_gui_offscreen_v1 kGuiExt = { guiOpen, guiClose, guiRender, guiReadPixels, guiMouse };
    static inline const lpi_latency_v1 kLatencyExt = { latencyGet };
    static inline const lpi_param_changes_v1 kParamChangesExt = { paramChangesGet };
    static inline const lpi_transport_v1 kTransportExt = { transportSet };
    static inline const lpi_plugin_api kApi = {
        create, destroy, activate, deactivate, process,
        paramCount, paramInfo, getParam, setParam,
        getState, setState, getExtension
    };
    static inline const lpi_plugin_factory kFactory = { { LPI_ABI_VERSION_MAJOR, LPI_ABI_VERSION_MINOR }, factoryInfo, factoryApi };
};

} // namespace lpi
} // namespace loudio

/* Defines the opaque lpi_plugin type and exports lpi_get_factory. Use once,
 * at namespace scope, in the plugin's lpi_<name>.cpp. */
#define LPI_DEFINE_PLUGIN(Traits) \
    struct lpi_plugin : loudio::lpi::Instance<Traits> {}; \
    extern "C" LPI_EXPORT const lpi_plugin_factory* lpi_get_factory(void) { \
        return loudio::lpi::Api<Traits, lpi_plugin>::factory(); \
    }
