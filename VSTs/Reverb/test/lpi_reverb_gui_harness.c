/*
 * Host-side test for the LPI Reverb DLL's lpi.gui.offscreen.v1 (#101/B4).
 * Loads lpi_reverb.dll, opens the offscreen GUI, injects a scripted knob
 * drag, dumps before/after frames as PPM files, and asserts the drag
 * actually reached ReverbCore (parameter value changed after a process()
 * block drains the queue) and visibly changed the frame.
 *
 * Output: lpi_reverb_before.ppm / lpi_reverb_after.ppm in the working dir.
 */
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lpi/lpi.h"

/* Not assert(): several checks below have side effects (activate, open,
 * render, read_pixels) and must run under NDEBUG too. */
#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); exit(1); } } while (0)

enum { kDecayTimeIndex = 1 }; /* kParamDecayTime in SharedStructures.h */

static void writePpm(const char* path, const unsigned char* rgba, int w, int h) {
    FILE* f = fopen(path, "wb");
    CHECK(f);
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; ++i) fwrite(rgba + i * 4, 1, 3, f);
    fclose(f);
}

static void processBlock(const lpi_plugin_api* api, lpi_plugin* inst) {
    float inL[512] = {0}, inR[512] = {0}, outL[512], outR[512];
    inL[0] = inR[0] = 1.0f;
    const float* ins[2] = { inL, inR };
    float* outs[2] = { outL, outR };
    lpi_process_data d = { ins, outs, 2, 2, 512 };
    api->process(inst, &d);
}

/*
 * Frame-cost measurement for the B6 frame-streaming budget (#97 targets
 * ~15-20fps ambient). Times render() and read_pixels() separately over
 * many frames and prints avg/max per call plus the raw bandwidth a
 * full-frame stream implies. Informational only -- no threshold: CI runs on
 * Mesa llvmpipe (software GL), so absolute numbers are a pessimistic upper
 * bound, not what a real GPU does. Its value is the trend between runs and
 * the render/readback split.
 */
static double nowMs(void) {
    static LARGE_INTEGER freq;
    LARGE_INTEGER t;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1000.0 / (double)freq.QuadPart;
}

static void measureFrameCost(const lpi_gui_offscreen_v1* gui, lpi_plugin* inst,
                             const lpi_gui_offscreen_info* info, const char* label) {
    enum { kFrames = 60 };
    size_t bytes = (size_t)info->physical_width * info->physical_height * 4;
    unsigned char* buf = (unsigned char*)malloc(bytes);
    CHECK(buf);
    double renderSum = 0, renderMax = 0, readSum = 0, readMax = 0;
    CHECK(gui->render(inst)); /* warm-up */
    CHECK(gui->read_pixels(inst, buf, bytes));
    for (int i = 0; i < kFrames; ++i) {
        double t0 = nowMs();
        CHECK(gui->render(inst));
        double t1 = nowMs();
        CHECK(gui->read_pixels(inst, buf, bytes));
        double t2 = nowMs();
        renderSum += t1 - t0; if (t1 - t0 > renderMax) renderMax = t1 - t0;
        readSum += t2 - t1;   if (t2 - t1 > readMax) readMax = t2 - t1;
    }
    double mbPerFrame = (double)bytes / (1024.0 * 1024.0);
    printf("frame cost [%s %ux%u]: render avg %.2f ms (max %.2f), read_pixels avg %.2f ms (max %.2f), "
           "%.2f MB/frame = %.1f MB/s at 20fps uncompressed\n",
           label, info->physical_width, info->physical_height,
           renderSum / kFrames, renderMax, readSum / kFrames, readMax, mbPerFrame, mbPerFrame * 20.0);
    free(buf);
}

int main(void) {
    HMODULE m = LoadLibraryW(L"lpi_reverb.dll");
    CHECK(m);
    lpi_get_factory_fn getFactory = (lpi_get_factory_fn)GetProcAddress(m, LPI_GET_FACTORY_SYMBOL_NAME);
    CHECK(getFactory);
    const lpi_plugin_factory* factory = getFactory();
    CHECK(factory->abi_version.major == LPI_ABI_VERSION_MAJOR);
    const lpi_plugin_api* api = factory->get_api();

    lpi_plugin* inst = api->create(48000.0, 512);
    CHECK(inst && api->activate(inst));

    lpi_parameter_info pinfo;
    CHECK(api->get_parameter_count(inst) == 23);
    CHECK(api->get_parameter_info(inst, kDecayTimeIndex, &pinfo));
    CHECK(strcmp(pinfo.id, "decayTime") == 0);

    const lpi_gui_offscreen_v1* gui = (const lpi_gui_offscreen_v1*)api->get_extension(inst, LPI_EXT_GUI_OFFSCREEN_V1);
    CHECK(gui);

    lpi_gui_offscreen_info info;
    CHECK(gui->open(inst, 1.0f, &info));
    CHECK(info.physical_width == info.logical_width); /* dpi 1.0 */
    size_t bytes = (size_t)info.physical_width * info.physical_height * 4;
    unsigned char* before = (unsigned char*)malloc(bytes);
    unsigned char* after = (unsigned char*)malloc(bytes);

    /* A second editor in the same process must be refused (v1 scope). */
    lpi_plugin* inst2 = api->create(48000.0, 512);
    lpi_gui_offscreen_info info2;
    CHECK(!gui->open(inst2, 1.0f, &info2));
    api->destroy(inst2);

    CHECK(gui->render(inst));
    CHECK(gui->read_pixels(inst, before, bytes));
    writePpm("lpi_reverb_before.ppm", before, (int)info.physical_width, (int)info.physical_height);

    float defaultDecay = api->get_parameter_value(inst, kDecayTimeIndex);

    /* lpi.latency.v1: no lookahead in the Reverb. */
    const lpi_latency_v1* latency = (const lpi_latency_v1*)api->get_extension(inst, LPI_EXT_LATENCY_V1);
    CHECK(latency);
    CHECK(latency->get_latency(inst) == 0);

    /* lpi.params.changes.v1: nothing queued before any editor input. */
    const lpi_param_changes_v1* changes = (const lpi_param_changes_v1*)api->get_extension(inst, LPI_EXT_PARAM_CHANGES_V1);
    CHECK(changes);
    lpi_param_change ch[8];
    CHECK(changes->get_parameter_changes(inst, ch, 8) == 0);

    /* Hero decay knob centre per ReverbLayout.h: (345,50) + (55,55). Drag up. */
    gui->mouse_event(inst, LPI_MOUSE_MOVE, 400.0f, 105.0f);
    gui->mouse_event(inst, LPI_MOUSE_DOWN, 400.0f, 105.0f);
    for (int i = 1; i <= 10; ++i) gui->mouse_event(inst, LPI_MOUSE_MOVE, 400.0f, 105.0f - 3.0f * i);

    /* Mid-drag: one coalesced entry for decayTime, gesture begun, not ended. */
    uint32_t n = changes->get_parameter_changes(inst, ch, 8);
    printf("mid-drag changes: %u (index %u, flags %u)\n", n, n ? ch[0].index : 0u, n ? ch[0].flags : 0u);
    CHECK(n == 1);
    CHECK(ch[0].index == kDecayTimeIndex);
    CHECK(ch[0].flags == LPI_PARAM_CHANGE_GESTURE_BEGIN);

    for (int i = 11; i <= 20; ++i) gui->mouse_event(inst, LPI_MOUSE_MOVE, 400.0f, 105.0f - 3.0f * i);
    gui->mouse_event(inst, LPI_MOUSE_UP, 400.0f, 45.0f);

    /* A zero-capacity drain writes nothing and leaves the entry queued. */
    CHECK(changes->get_parameter_changes(inst, ch, 0) == 0);

    /* Queued edits are applied at the top of the next process() block. */
    processBlock(api, inst);
    float newDecay = api->get_parameter_value(inst, kDecayTimeIndex);
    printf("decayTime: %.3f -> %.3f\n", defaultDecay, newDecay);
    CHECK(fabsf(newDecay - defaultDecay) > 0.05f);

    /* Release: END only (BEGIN was already drained), at the settled value. */
    n = changes->get_parameter_changes(inst, ch, 8);
    CHECK(n == 1);
    CHECK(ch[0].index == kDecayTimeIndex);
    CHECK(ch[0].flags == LPI_PARAM_CHANGE_GESTURE_END);
    CHECK(ch[0].value == newDecay);
    CHECK(changes->get_parameter_changes(inst, ch, 8) == 0); /* drained */

    /* Host-originated writes are never echoed back. */
    CHECK(api->set_parameter_value(inst, kDecayTimeIndex, 3.0f));
    processBlock(api, inst);
    CHECK(changes->get_parameter_changes(inst, ch, 8) == 0);
    CHECK(api->set_parameter_value(inst, kDecayTimeIndex, newDecay)); /* restore for the frame check below */
    processBlock(api, inst);

    CHECK(gui->render(inst));
    CHECK(gui->read_pixels(inst, after, bytes));
    writePpm("lpi_reverb_after.ppm", after, (int)info.physical_width, (int)info.physical_height);
    CHECK(memcmp(before, after, bytes) != 0);

    /* Orientation guard: the only widget that changed is the decay knob,
     * rect (345,50,110,110) in ReverbLayout.h. With top-left rows (the
     * read_pixels contract) every differing pixel lands inside it; a
     * bottom-up frame would put them around y = 440..550 and fail here. */
    {
        int w = (int)info.physical_width, h = (int)info.physical_height;
        int minX = w, minY = h, maxX = -1, maxY = -1;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                size_t i = ((size_t)y * (size_t)w + (size_t)x) * 4;
                if (memcmp(before + i, after + i, 4) != 0) {
                    if (x < minX) minX = x; if (x > maxX) maxX = x;
                    if (y < minY) minY = y; if (y > maxY) maxY = y;
                }
            }
        }
        printf("changed pixels: x %d..%d, y %d..%d\n", minX, maxX, minY, maxY);
        const int margin = 4; /* anti-aliasing bleed */
        CHECK(maxX >= 0);
        CHECK(minX >= 345 - margin && maxX <= 345 + 110 + margin);
        CHECK(minY >= 50 - margin && maxY <= 50 + 110 + margin);
    }

    /* Double-click the decay knob: resets to its default (2.0 s) through the
     * same injected mouse path a streaming host uses, and the reset is
     * reported as one gesture (BEGIN on the reset, END on release). */
    changes->get_parameter_changes(inst, ch, 8); /* drain */
    /* A click on empty panel first, so the earlier drag's mouse-down on this
     * knob (milliseconds ago on CI) can't pair with the first click below. */
    gui->mouse_event(inst, LPI_MOUSE_DOWN, 700.0f, 580.0f);
    gui->mouse_event(inst, LPI_MOUSE_UP, 700.0f, 580.0f);
    gui->mouse_event(inst, LPI_MOUSE_DOWN, 400.0f, 105.0f);
    gui->mouse_event(inst, LPI_MOUSE_UP, 400.0f, 105.0f);
    gui->mouse_event(inst, LPI_MOUSE_DOWN, 400.0f, 105.0f);
    gui->mouse_event(inst, LPI_MOUSE_UP, 400.0f, 105.0f);
    processBlock(api, inst);
    printf("double-click: decayTime %.3f -> %.3f\n", newDecay, api->get_parameter_value(inst, kDecayTimeIndex));
    CHECK(fabsf(api->get_parameter_value(inst, kDecayTimeIndex) - defaultDecay) < 1e-4f); /* widget->param round trip */
    n = changes->get_parameter_changes(inst, ch, 8);
    CHECK(n == 1 && ch[0].index == kDecayTimeIndex && fabsf(ch[0].value - defaultDecay) < 1e-4f);
    CHECK(ch[0].flags == (LPI_PARAM_CHANGE_GESTURE_BEGIN | LPI_PARAM_CHANGE_GESTURE_END));

    measureFrameCost(gui, inst, &info, "1.0x");
    gui->close(inst);

    /* Same at 2.0x (a HiDPI browser client): 4x the pixels to render and ship. */
    lpi_gui_offscreen_info info2x;
    CHECK(gui->open(inst, 2.0f, &info2x));
    CHECK(info2x.physical_width == 2 * info2x.logical_width);
    measureFrameCost(gui, inst, &info2x, "2.0x");
    gui->close(inst);
    api->deactivate(inst);
    api->destroy(inst);
    free(before);
    free(after);
    FreeLibrary(m);
    printf("LPI Reverb offscreen GUI harness passed.\n");
    return 0;
}
