/*
 * LPI conformance suite: loads ANY LPI plugin DLL and checks the generic
 * contract in lpi.h -- nothing plugin-specific. Hosts (e.g. noprod's
 * GhostDAW) can vendor this file to vet a plugin before loading it.
 *
 *   lpi_conformance <plugin.dll>
 *
 * Reports every failed check rather than stopping at the first, then exits
 * non-zero if any failed. Optional extensions are exercised only when the
 * plugin provides them.
 *
 * Deliberately NOT checked here (contract doesn't require it, or covered
 * elsewhere): exact DSP output, value clamping, real-time safety under
 * contention (see test/rt_safety), state rejection of malformed blobs.
 */
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lpi/lpi.h"

static int g_failures = 0;
static int g_checks = 0;

#define EXPECT(cond, ...) do { ++g_checks; if (!(cond)) { ++g_failures; \
    printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("  [%s]\n", #cond); } } while (0)

enum { kMaxBlock = 512, kMaxChannels = 8 };
static const double kSampleRate = 48000.0;

typedef struct {
    float in[kMaxChannels][kMaxBlock];
    float out[kMaxChannels][kMaxBlock];
    const float* inPtrs[kMaxChannels];
    float* outPtrs[kMaxChannels];
} Buffers;

static void runBlock(const lpi_plugin_api* api, lpi_plugin* inst, const lpi_plugin_info* info,
                     Buffers* b, uint32_t frames, unsigned* seed) {
    uint32_t nin = info->num_audio_inputs < kMaxChannels ? info->num_audio_inputs : kMaxChannels;
    uint32_t nout = info->num_audio_outputs < kMaxChannels ? info->num_audio_outputs : kMaxChannels;
    for (uint32_t c = 0; c < kMaxChannels; ++c) {
        for (uint32_t i = 0; i < frames; ++i) {
            *seed = *seed * 1664525u + 1013904223u;
            b->in[c][i] = ((float)(*seed >> 8) / 16777216.0f) * 2.0f - 1.0f;
            b->out[c][i] = 0.0f;
        }
        b->inPtrs[c] = b->in[c];
        b->outPtrs[c] = b->out[c];
    }
    lpi_process_data d = { nin ? b->inPtrs : NULL, b->outPtrs, nin, nout, frames };
    api->process(inst, &d);
}

static int outputsFinite(const lpi_plugin_info* info, const Buffers* b, uint32_t frames) {
    uint32_t nout = info->num_audio_outputs < kMaxChannels ? info->num_audio_outputs : kMaxChannels;
    for (uint32_t c = 0; c < nout; ++c)
        for (uint32_t i = 0; i < frames; ++i)
            if (!isfinite(b->out[c][i])) return 0;
    return 1;
}

/* A value strictly inside the range (not the default), respecting flags. */
static float probeValue(const lpi_parameter_info* p) {
    if (p->flags & LPI_PARAM_BOOLEAN) return p->default_value > 0.5f ? 0.0f : 1.0f;
    float v = p->min_value + 0.75f * (p->max_value - p->min_value);
    if (p->flags & LPI_PARAM_STEPPED) v = floorf(v + 0.5f);
    if (fabsf(v - p->default_value) < 1e-6f) v = p->min_value + 0.25f * (p->max_value - p->min_value);
    if (p->flags & LPI_PARAM_STEPPED) v = floorf(v + 0.5f);
    return v;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: lpi_conformance <plugin.dll>\n");
        return 2;
    }
    printf("LPI conformance: %s\n", argv[1]);

    /* ---- Loading & factory ---- */
    HMODULE m = LoadLibraryA(argv[1]);
    if (!m) { printf("  FAIL: LoadLibrary error %lu\n", GetLastError()); return 1; }
    lpi_get_factory_fn getFactory = (lpi_get_factory_fn)GetProcAddress(m, LPI_GET_FACTORY_SYMBOL_NAME);
    if (!getFactory) { printf("  FAIL: no exported %s\n", LPI_GET_FACTORY_SYMBOL_NAME); return 1; }
    const lpi_plugin_factory* factory = getFactory();
    if (!factory) { printf("  FAIL: factory is NULL\n"); return 1; }
    EXPECT(factory->abi_version.major == LPI_ABI_VERSION_MAJOR, "ABI major %u, host expects %u",
           factory->abi_version.major, LPI_ABI_VERSION_MAJOR);
    EXPECT(factory->get_info && factory->get_api, "factory function pointers");
    if (g_failures) return 1;

    const lpi_plugin_info* info = factory->get_info();
    const lpi_plugin_api* api = factory->get_api();
    EXPECT(info && info->id && info->id[0] && info->name && info->name[0], "plugin info id/name present");
    EXPECT(api && api->create && api->destroy && api->activate && api->deactivate && api->process &&
           api->get_parameter_count && api->get_parameter_info && api->get_parameter_value &&
           api->set_parameter_value && api->get_state && api->set_state && api->get_extension,
           "every vtable entry is non-NULL");
    if (g_failures) return 1;
    printf("  plugin: %s (%s), %u in / %u out\n", info->name, info->id, info->num_audio_inputs, info->num_audio_outputs);

    lpi_plugin* inst = api->create(kSampleRate, kMaxBlock);
    EXPECT(inst != NULL, "create()");
    if (!inst) return 1;
    EXPECT(api->activate(inst), "activate()");

    /* ---- Parameters ---- */
    uint32_t count = api->get_parameter_count(inst);
    printf("  parameters: %u\n", count);
    lpi_parameter_info* params = (lpi_parameter_info*)calloc(count ? count : 1, sizeof(lpi_parameter_info));
    for (uint32_t i = 0; i < count; ++i) {
        lpi_parameter_info* p = &params[i];
        EXPECT(api->get_parameter_info(inst, i, p), "get_parameter_info(%u)", i);
        EXPECT(p->index == i, "param %u reports index %u", i, p->index);
        EXPECT(p->id && p->id[0] && p->name && p->name[0], "param %u id/name present", i);
        EXPECT(p->min_value <= p->default_value && p->default_value <= p->max_value,
               "param %u ('%s') default %g within [%g, %g]", i, p->id ? p->id : "?", p->default_value, p->min_value, p->max_value);
        EXPECT((p->flags & ~(uint32_t)(LPI_PARAM_BOOLEAN | LPI_PARAM_STEPPED | LPI_PARAM_READONLY)) == 0,
               "param %u has only known flags (0x%x)", i, p->flags);
        float v = api->get_parameter_value(inst, i);
        EXPECT(fabsf(v - p->default_value) < 1e-5f, "param %u ('%s') starts at default %g, got %g", i, p->id ? p->id : "?", p->default_value, v);
        for (uint32_t j = 0; j < i; ++j) {
            EXPECT(!(p->id && params[j].id && strcmp(p->id, params[j].id) == 0),
                   "param ids unique ('%s' at %u and %u)", p->id ? p->id : "?", j, i);
        }
    }
    lpi_parameter_info dummy;
    EXPECT(!api->get_parameter_info(inst, count, &dummy), "get_parameter_info(count) fails");
    EXPECT(!api->set_parameter_value(inst, count, 0.0f), "set_parameter_value(count) fails");

    /* set_parameter_value applies by the next process() block */
    Buffers* b = (Buffers*)calloc(1, sizeof(Buffers));
    unsigned seed = 12345;
    for (uint32_t i = 0; i < count; ++i) {
        if (params[i].flags & LPI_PARAM_READONLY) continue;
        EXPECT(api->set_parameter_value(inst, i, probeValue(&params[i])), "set_parameter_value(%u)", i);
    }
    runBlock(api, inst, info, b, 64, &seed);
    for (uint32_t i = 0; i < count; ++i) {
        if (params[i].flags & LPI_PARAM_READONLY) continue;
        float want = probeValue(&params[i]), got = api->get_parameter_value(inst, i);
        EXPECT(fabsf(got - want) <= 1e-4f * (1.0f + fabsf(want)),
               "param %u ('%s') set to %g, reads %g after process()", i, params[i].id, want, got);
    }

    /* ---- Processing ---- */
    const uint32_t sizes[] = { 1, 7, 64, 333, kMaxBlock };
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        for (int rep = 0; rep < 8; ++rep) runBlock(api, inst, info, b, sizes[s], &seed);
        EXPECT(outputsFinite(info, b, sizes[s]), "outputs finite on noise, block size %u", sizes[s]);
    }

    /* ---- State ---- */
    size_t stateSize = api->get_state(inst, NULL, 0);
    EXPECT(stateSize > 0, "get_state(NULL) reports a size");
    if (stateSize > 0) {
        unsigned char* state = (unsigned char*)malloc(stateSize);
        EXPECT(api->get_state(inst, state, stateSize) == stateSize, "get_state fills exactly the reported size");
        EXPECT(stateSize == 1 || api->get_state(inst, state, stateSize - 1) == 0, "get_state rejects an undersized buffer");
        EXPECT(!api->set_state(inst, NULL, 0), "set_state(NULL, 0) fails");

        lpi_plugin* fresh = api->create(kSampleRate, kMaxBlock);
        EXPECT(fresh && api->activate(fresh), "second instance");
        if (fresh) {
            EXPECT(api->set_state(fresh, state, stateSize), "set_state round-trip onto fresh instance");
            for (uint32_t i = 0; i < count; ++i) {
                if (params[i].flags & LPI_PARAM_READONLY) continue;
                float a = api->get_parameter_value(inst, i), c = api->get_parameter_value(fresh, i);
                EXPECT(fabsf(a - c) <= 1e-5f * (1.0f + fabsf(a)), "state round-trips param %u ('%s'): %g vs %g", i, params[i].id, a, c);
            }
            api->deactivate(fresh);
            api->destroy(fresh);
        }
        free(state);
    }

    /* ---- Extensions ---- */
    EXPECT(api->get_extension(inst, "lpi.conformance.no-such-extension") == NULL, "unknown extension -> NULL");

    const lpi_latency_v1* latency = (const lpi_latency_v1*)api->get_extension(inst, LPI_EXT_LATENCY_V1);
    if (latency) {
        uint32_t l = latency->get_latency(inst);
        printf("  %s: %u samples\n", LPI_EXT_LATENCY_V1, l);
        EXPECT(latency->get_latency && l < (uint32_t)(kSampleRate * 10), "latency sane (<10 s)");
        EXPECT(latency->get_latency(inst) == l, "latency stable within an activation");
    }

    const lpi_param_changes_v1* changes = (const lpi_param_changes_v1*)api->get_extension(inst, LPI_EXT_PARAM_CHANGES_V1);
    if (changes) {
        printf("  %s: present\n", LPI_EXT_PARAM_CHANGES_V1);
        lpi_param_change ch[16];
        changes->get_parameter_changes(inst, ch, 16); /* drain anything pending */
        if (count > 0) {
            for (uint32_t i = 0; i < count; ++i) {
                if (params[i].flags & LPI_PARAM_READONLY) continue;
                api->set_parameter_value(inst, i, params[i].default_value);
                break;
            }
            runBlock(api, inst, info, b, 64, &seed);
        }
        EXPECT(changes->get_parameter_changes(inst, ch, 16) == 0, "host set_parameter_value is not echoed");
        EXPECT(changes->get_parameter_changes(inst, NULL, 16) == 0, "NULL out buffer writes nothing");
    }

    const lpi_gui_offscreen_v1* gui = (const lpi_gui_offscreen_v1*)api->get_extension(inst, LPI_EXT_GUI_OFFSCREEN_V1);
    if (gui) {
        printf("  %s: present\n", LPI_EXT_GUI_OFFSCREEN_V1);
        for (int round = 0; round < 2; ++round) { /* open/close twice: lifecycle is repeatable */
            lpi_gui_offscreen_info gi;
            int opened = gui->open(inst, 1.0f, &gi);
            EXPECT(opened, "gui open (round %d)", round);
            if (!opened) break;
            EXPECT(gi.logical_width > 0 && gi.logical_height > 0, "gui logical size");
            EXPECT(gi.physical_width == gi.logical_width && gi.physical_height == gi.logical_height, "physical == logical at 1.0x");
            size_t bytes = (size_t)gi.physical_width * gi.physical_height * 4;
            unsigned char* px = (unsigned char*)malloc(bytes);
            EXPECT(gui->render(inst), "gui render");
            EXPECT(gui->read_pixels(inst, px, bytes), "gui read_pixels");
            EXPECT(!gui->read_pixels(inst, px, bytes - 1), "gui read_pixels rejects undersized buffer");
            gui->mouse_event(inst, LPI_MOUSE_MOVE, 1.0f, 1.0f);
            EXPECT(gui->render(inst), "gui render after input");
            free(px);
            gui->close(inst);
        }
    }

    api->deactivate(inst);
    api->destroy(inst);
    free(params);
    free(b);
    FreeLibrary(m);

    printf("%d/%d checks passed%s\n", g_checks - g_failures, g_checks, g_failures ? "" : " -- conformant");
    return g_failures ? 1 : 0;
}
