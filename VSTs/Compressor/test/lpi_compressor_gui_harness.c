/*
 * Host-side test for lpi_compressor.dll's lpi.gui.offscreen.v1: loads the DLL,
 * opens the offscreen GUI, drags the Makeup knob, and checks the drag reached
 * the core (through lpi.params.changes.v1 with gesture flags) and changed
 * the frame. Dumps before/after frames as PPM files in the working dir.
 */
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lpi/lpi.h"

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); exit(1); } } while (0)

enum { kGainIndex = 6 }; /* kCompressorMakeup in CompressorParams.h */
/* Makeup knob centre, from kCompressorMakeupKnob* / kCompressorKnobSize in CompressorUi.h. */
static const float kKnobCentreX = 550.0f + 40.0f;
static const float kKnobCentreY = 130.0f + 40.0f;

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
    HMODULE m = LoadLibraryW(L"lpi_compressor.dll");
    CHECK(m);
    lpi_get_factory_fn getFactory = (lpi_get_factory_fn)GetProcAddress(m, LPI_GET_FACTORY_SYMBOL_NAME);
    CHECK(getFactory);
    const lpi_plugin_factory* factory = getFactory();
    CHECK(factory->abi_version.major == LPI_ABI_VERSION_MAJOR);
    CHECK(strcmp(factory->get_info()->id, "com.loudio.compressor") == 0);
    const lpi_plugin_api* api = factory->get_api();

    lpi_plugin* inst = api->create(48000.0, 512);
    CHECK(inst && api->activate(inst));

    lpi_parameter_info pinfo;
    CHECK(api->get_parameter_info(inst, kGainIndex, &pinfo));
    CHECK(strcmp(pinfo.id, "makeup") == 0);

    const lpi_gui_offscreen_v1* gui = (const lpi_gui_offscreen_v1*)api->get_extension(inst, LPI_EXT_GUI_OFFSCREEN_V1);
    CHECK(gui);
    const lpi_param_changes_v1* changes = (const lpi_param_changes_v1*)api->get_extension(inst, LPI_EXT_PARAM_CHANGES_V1);
    CHECK(changes);

    lpi_gui_offscreen_info info;
    CHECK(gui->open(inst, 1.0f, &info));
    CHECK(info.logical_width == 920 && info.logical_height == 420);
    CHECK(info.physical_width == info.logical_width);
    size_t bytes = (size_t)info.physical_width * info.physical_height * 4;
    unsigned char* before = (unsigned char*)malloc(bytes);
    unsigned char* after = (unsigned char*)malloc(bytes);
    CHECK(before && after);

    CHECK(gui->render(inst));
    CHECK(gui->read_pixels(inst, before, bytes));
    writePpm("lpi_compressor_before.ppm", before, (int)info.physical_width, (int)info.physical_height);

    float defaultGain = api->get_parameter_value(inst, kGainIndex);
    lpi_param_change ch[8];
    CHECK(changes->get_parameter_changes(inst, ch, 8) == 0);

    /* Drag the gain knob up. */
    gui->mouse_event(inst, LPI_MOUSE_MOVE, kKnobCentreX, kKnobCentreY);
    gui->mouse_event(inst, LPI_MOUSE_DOWN, kKnobCentreX, kKnobCentreY);
    for (int i = 1; i <= 10; ++i) gui->mouse_event(inst, LPI_MOUSE_MOVE, kKnobCentreX, kKnobCentreY - 3.0f * i);
    uint32_t n = changes->get_parameter_changes(inst, ch, 8);
    CHECK(n == 1 && ch[0].index == kGainIndex && ch[0].flags == LPI_PARAM_CHANGE_GESTURE_BEGIN);
    gui->mouse_event(inst, LPI_MOUSE_UP, kKnobCentreX, kKnobCentreY - 30.0f);

    processBlock(api, inst); /* drains the core's queue */
    float newGain = api->get_parameter_value(inst, kGainIndex);
    CHECK(newGain > defaultGain + 0.01f);
    n = changes->get_parameter_changes(inst, ch, 8);
    CHECK(n == 1 && ch[0].index == kGainIndex && ch[0].flags == LPI_PARAM_CHANGE_GESTURE_END && ch[0].value == newGain);

    CHECK(gui->render(inst));
    CHECK(gui->read_pixels(inst, after, bytes));
    writePpm("lpi_compressor_after.ppm", after, (int)info.physical_width, (int)info.physical_height);
    CHECK(memcmp(before, after, bytes) != 0);

    gui->close(inst);
    api->deactivate(inst);
    api->destroy(inst);
    free(before);
    free(after);
    FreeLibrary(m);
    printf("LPI Compressor offscreen GUI harness passed.\n");
    return 0;
}
