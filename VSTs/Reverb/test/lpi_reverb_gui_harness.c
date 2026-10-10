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

/* Not CHECK(): several checks below have side effects (activate, open,
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

    /* Hero decay knob centre per ReverbLayout.h: (345,50) + (55,55). Drag up. */
    gui->mouse_event(inst, LPI_MOUSE_MOVE, 400.0f, 105.0f);
    gui->mouse_event(inst, LPI_MOUSE_DOWN, 400.0f, 105.0f);
    for (int i = 1; i <= 20; ++i) gui->mouse_event(inst, LPI_MOUSE_MOVE, 400.0f, 105.0f - 3.0f * i);
    gui->mouse_event(inst, LPI_MOUSE_UP, 400.0f, 45.0f);

    /* Queued edits are applied at the top of the next process() block. */
    processBlock(api, inst);
    float newDecay = api->get_parameter_value(inst, kDecayTimeIndex);
    printf("decayTime: %.3f -> %.3f\n", defaultDecay, newDecay);
    CHECK(fabsf(newDecay - defaultDecay) > 0.05f);

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

    gui->close(inst);
    api->deactivate(inst);
    api->destroy(inst);
    free(before);
    free(after);
    FreeLibrary(m);
    printf("LPI Reverb offscreen GUI harness passed.\n");
    return 0;
}
