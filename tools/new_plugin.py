#!/usr/bin/env python3
"""Scaffold a new plugin under VSTs/<Name> on the plugin platform (P4, #130).

    tools/new_plugin.py <Name> [--id com.loudio.<name>] [--code Xxx1] [--product "Loudio <Name>"]

Generates a complete, buildable plugin that passes every test before any
DSP is written: a core on the P1 contract (DSP/include/loudio/plugin) with
a one-parameter table (gain) and a transport hook, a one-knob cuif UI, the
LPI build on the P2 wrapper template, the VST3/Standalone build on the P3
shared JUCE wrapper, DSPTests.cpp wired to the generic core test kit
(golden IR with --regen-golden, zero-allocation hook, state round-trip),
the LPI offscreen GUI harness, the golden IR for the pass-through core,
and the plugin's CMakeLists.txt. CI picks the plugin up automatically
(tools/list_plugins.py); deleting VSTs/<Name> leaves no trace.

Plain Python 3, no dependencies (spike §5.6 decision).
"""
import argparse
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# --------------------------------------------------------------------------
# Templates. Tokens: @Name@ (Scratch), @lower@ (scratch), @upper@ (SCRATCH),
# @id@ (com.loudio.scratch), @code@ (Scr1), @product@ (Loudio Scratch),
# @magic@ (4-char state magic).
# --------------------------------------------------------------------------

CMAKE = r'''cmake_minimum_required(VERSION 3.21)
project(Loudio@Name@ VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Shared DSP building blocks + plugin platform (DSP/include/loudio), header-only.
set(LOUDIO_DSP_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../../DSP/include)
set(LOUDIO_FRAMEWORK_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../Framework/src)
set(LOUDIO_CUIF_SOURCES
    ${LOUDIO_FRAMEWORK_SRC}/graphics.c
    ${LOUDIO_FRAMEWORK_SRC}/font.c
    ${LOUDIO_FRAMEWORK_SRC}/widget.c
    ${LOUDIO_FRAMEWORK_SRC}/window_win32.c
    ${LOUDIO_FRAMEWORK_SRC}/theme.c
    ${LOUDIO_FRAMEWORK_SRC}/theme_hello_kitty.c
    ${LOUDIO_FRAMEWORK_SRC}/theme_greens.c
)

# ON: build only dsp_tests -- no JUCE, no plugin targets (the Linux CI job).
option(LOUDIO_PLUGIN_TESTS_ONLY "Build only the DSP tests (skips JUCE and all plugin targets)" OFF)

if(NOT LOUDIO_PLUGIN_TESTS_ONLY)
# The JUCE submodule is shared by every plugin: VSTs/JUCE.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../JUCE ${CMAKE_BINARY_DIR}/juce)

juce_add_plugin(Loudio@Name@
    PRODUCT_NAME "@product@"
    COMPANY_NAME "Loudio"
    PLUGIN_MANUFACTURER_CODE Loud
    PLUGIN_CODE @code@
    FORMATS VST3 Standalone
    VST3_CATEGORIES Fx
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
)

juce_generate_juce_header(Loudio@Name@)

target_sources(Loudio@Name@ PRIVATE
    src/PluginProcessor.cpp
    src/@Name@Ui.cpp
    src/@Name@Core.cpp
    ${LOUDIO_CUIF_SOURCES}
)

target_include_directories(Loudio@Name@ PRIVATE src ../common ../../Framework/include ${LOUDIO_DSP_INCLUDE})

target_compile_definitions(Loudio@Name@ PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Loudio@Name@ PRIVATE
    juce::juce_dsp
    juce::juce_gui_basics
    opengl32
PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# juce_audio_utils only for the Standalone's device/file glue (see Reverb, #94).
if(TARGET Loudio@Name@_Standalone)
    target_link_libraries(Loudio@Name@_Standalone PRIVATE juce::juce_audio_utils)
endif()
endif() # NOT LOUDIO_PLUGIN_TESTS_ONLY

option(LOUDIO_@upper@_BUILD_TESTS "Build DSP unit tests executable" ON)
if(LOUDIO_@upper@_BUILD_TESTS)
    add_executable(dsp_tests
        src/DSPTests.cpp
        src/@Name@Core.cpp
    )
    target_include_directories(dsp_tests PRIVATE src ../../Framework/include ${LOUDIO_DSP_INCLUDE})
    # NOMINMAX: cuif/ring_buffer.h pulls in <windows.h>, whose min/max
    # macros otherwise break std::min/std::max.
    target_compile_definitions(dsp_tests PRIVATE
        NOMINMAX
        LOUDIO_@upper@_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/golden"
    )
endif()

# LPI (Loudio Plugin Interface) build: no JUCE, on PluginABI/wrapper's template.
option(LOUDIO_@upper@_BUILD_LPI "Build the LPI-ABI DLL and its offscreen GUI test harness" ON)
if(LOUDIO_@upper@_BUILD_LPI AND WIN32)
    set(LPI_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

    add_library(lpi_@lower@ SHARED
        src/lpi_@lower@.cpp
        src/@Name@Ui.cpp
        src/@Name@Core.cpp
        ${LOUDIO_CUIF_SOURCES}
    )
    target_include_directories(lpi_@lower@ PRIVATE src ../../Framework/include ../../PluginABI/include ../../PluginABI/wrapper ${LOUDIO_DSP_INCLUDE})
    target_compile_definitions(lpi_@lower@ PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(lpi_@lower@ PRIVATE opengl32 gdi32 user32)
    set_target_properties(lpi_@lower@ PROPERTIES
        OUTPUT_NAME "lpi_@lower@"
        PREFIX "" # MinGW would otherwise emit liblpi_@lower@.dll; the harness loads lpi_@lower@.dll
        RUNTIME_OUTPUT_DIRECTORY "${LPI_OUTPUT_DIR}"
    )

    # PluginABI's generic conformance suite, run here against lpi_@lower@.dll.
    add_executable(lpi_conformance ../../PluginABI/test/conformance/lpi_conformance.c)
    target_include_directories(lpi_conformance PRIVATE ../../PluginABI/include)
    add_dependencies(lpi_conformance lpi_@lower@)
    set_target_properties(lpi_conformance PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${LPI_OUTPUT_DIR}")

    add_executable(lpi_@lower@_gui_harness test/lpi_@lower@_gui_harness.c)
    target_include_directories(lpi_@lower@_gui_harness PRIVATE ../../PluginABI/include)
    add_dependencies(lpi_@lower@_gui_harness lpi_@lower@)
    set_target_properties(lpi_@lower@_gui_harness PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${LPI_OUTPUT_DIR}")
endif()
'''

PARAMS_H = r'''#pragma once

#include "loudio/plugin/ParamTable.h"

/*
 * @Name@'s parameter table: the single source of truth for every
 * parameter's id, name, range, default, step and flags. The APVTS layout,
 * the LPI parameter info, the state blob and the UI mapping all read from
 * it (DSP/include/loudio/plugin/ParamTable.h).
 *
 * Order matches @Name@ParamIndex exactly (asserted by DSPTests.cpp). Add
 * parameters at the END and never rename an id: state blobs key on it.
 */
enum @Name@ParamIndex {
    k@Name@Gain = 0,
    k@Name@NumParams
};

static const loudio::ParamMeta k@Name@Params[k@Name@NumParams] = {
    { k@Name@Gain, "gain", "Gain", 0.0f, 2.0f, 1.0f, 0.01f, 0 },
};

static const loudio::ParamTable k@Name@ParamTable = { k@Name@Params, k@Name@NumParams };
'''

CORE_H = r'''#pragma once

#include "@Name@Params.h"
#include "loudio/plugin/ParamQueue.h"
#include "loudio/plugin/Transport.h"

/*
 * @Name@'s DSP engine on the platform core contract
 * (DSP/include/loudio/plugin/CORE_CONTRACT.md). JUCE-free and LPI-free:
 * both wrappers (VSTs/common for VST3, PluginABI/wrapper for LPI) are
 * generated around it.
 *
 * Scaffold state: a stereo pass-through times `gain`. Replace process()
 * (and prepare()) with the real DSP, extend @Name@Params.h, then
 * regenerate the golden impulse responses:  dsp_tests --regen-golden
 */
class @Name@Core {
public:
    @Name@Core();

    void prepare(double sampleRate, int maxBlockSize);
    void process(float* left, float* right, int numSamples);

    static const loudio::ParamTable& table() { return k@Name@ParamTable; }
    int getParameterCount() const { return k@Name@NumParams; }
    bool getParameterInfo(int index, loudio::ParamMeta* outMeta) const;
    float getParameterValue(int index) const;

    void setParameterValueDirect(int index, float value);
    bool setParameterValue(int index, float value);
    bool setParameterValues(const loudio::ParamUpdate* updates, size_t count);

    size_t getState(void* buffer, size_t bufferSize) const;
    bool setState(const void* data, size_t size);

    /* Host transport, once per block before process() (lpi.transport.v1 /
     * JUCE playhead). Delete this if the plugin never needs tempo. */
    void setTransport(const loudio::TransportInfo& t) { transport = t; }

private:
    double sampleRate = 44100.0;
    float params[k@Name@NumParams];
    loudio::TransportInfo transport;
    loudio::ParamQueue<1024> paramQueue;
};
'''

CORE_CPP = r'''#include "@Name@Core.h"
#include "loudio/plugin/StateBlob.h"

namespace {
const char kStateMagic[4] = { '@magic@' };
} // namespace

@Name@Core::@Name@Core() {
    k@Name@ParamTable.fillDefaults(params);
}

void @Name@Core::prepare(double newSampleRate, int /* maxBlockSize */) {
    sampleRate = newSampleRate;
}

void @Name@Core::process(float* left, float* right, int numSamples) {
    paramQueue.drainInto(k@Name@ParamTable, params);

    const float gain = params[k@Name@Gain];
    for (int i = 0; i < numSamples; ++i) {
        left[i] *= gain;
        right[i] *= gain;
    }
}

bool @Name@Core::getParameterInfo(int index, loudio::ParamMeta* outMeta) const {
    if (!k@Name@ParamTable.valid(index) || !outMeta) return false;
    *outMeta = k@Name@ParamTable[index];
    return true;
}

float @Name@Core::getParameterValue(int index) const {
    return k@Name@ParamTable.valid(index) ? params[index] : 0.0f;
}

void @Name@Core::setParameterValueDirect(int index, float value) {
    if (k@Name@ParamTable.valid(index)) params[index] = value;
}

bool @Name@Core::setParameterValue(int index, float value) {
    return paramQueue.push(k@Name@ParamTable, index, value);
}

bool @Name@Core::setParameterValues(const loudio::ParamUpdate* updates, size_t count) {
    return paramQueue.pushAll(k@Name@ParamTable, updates, count);
}

size_t @Name@Core::getState(void* buffer, size_t bufferSize) const {
    return loudio::StateBlob::write(k@Name@ParamTable, kStateMagic, params, buffer, bufferSize);
}

bool @Name@Core::setState(const void* data, size_t size) {
    return loudio::StateBlob::read(k@Name@ParamTable, kStateMagic, data, size, params);
}
'''

UI_H = r'''#pragma once

#include "@Name@Params.h"
#include "cuif/cuif.h"
#include <cstddef>

/*
 * @Name@'s widget tree, shared by the VST3 editor (VSTs/common) and the
 * LPI offscreen editor (PluginABI/wrapper). JUCE-free; UI thread only.
 *
 * The UI never writes parameters itself: every edit goes out through
 * sendParams (batched where edits belong together) and each host keeps
 * its own RT-safe path to the core.
 */
typedef void (*@Name@SendParamsFn)(void* context, const loudio::ParamUpdate* updates, size_t count);

struct @Name@UiCallbacks {
    void* context;
    @Name@SendParamsFn sendParams;
};

/* Layout in logical pixels. The GUI harness drags the gain knob at its
 * centre, so keep these in sync with test/lpi_@lower@_gui_harness.c. */
constexpr int k@Name@PanelWidth = 400;
constexpr int k@Name@PanelHeight = 240;
constexpr float k@Name@GainKnobX = 150.0f, k@Name@GainKnobY = 60.0f, k@Name@GainKnobSize = 100.0f;

struct @Name@Ui {
    @Name@UiCallbacks callbacks;
    cuif_widget* root = nullptr;
    cuif_widget* knobs[k@Name@NumParams] = {};
};

@Name@Ui* @lower@UiCreate(float panelWidth, float panelHeight, @Name@UiCallbacks callbacks);
void @lower@UiDestroy(@Name@Ui* ui);
/* Pushes current parameter values into every widget. Never calls sendParams. */
void @lower@UiSyncFromParams(@Name@Ui* ui, const float* params);
'''

UI_CPP = r'''#include "@Name@Ui.h"

#include <cstdint>
#include <new>

namespace {

/* The root carries the @Name@Ui in user_data; each knob's user_data is its
 * parameter index, so one callback serves every knob. */
@Name@Ui* uiOf(cuif_widget* w) {
    return (w && w->parent) ? static_cast<@Name@Ui*>(w->parent->user_data) : nullptr;
}

void knobChanged(cuif_widget* w, float value) {
    if (@Name@Ui* ui = uiOf(w)) {
        int index = (int)(intptr_t)w->user_data;
        loudio::ParamUpdate msg = { index, k@Name@ParamTable.fromWidget(index, value) };
        if (ui->callbacks.sendParams) ui->callbacks.sendParams(ui->callbacks.context, &msg, 1);
    }
}

} // namespace

@Name@Ui* @lower@UiCreate(float w, float h, @Name@UiCallbacks callbacks) {
    @Name@Ui* ui = new (std::nothrow) @Name@Ui();
    if (!ui) return nullptr;
    ui->callbacks = callbacks;

    ui->root = cuif_widget_create_container(0, 0, w, h);
    ui->root->user_data = ui;

    const loudio::ParamMeta& gain = k@Name@ParamTable[k@Name@Gain];
    float initial = k@Name@ParamTable.toWidget(k@Name@Gain, gain.defaultValue);
    cuif_widget* knob = cuif_widget_create_knob(k@Name@GainKnobX, k@Name@GainKnobY, k@Name@GainKnobSize, k@Name@GainKnobSize,
                                                gain.name, initial, knobChanged);
    knob->user_data = (void*)(intptr_t)k@Name@Gain;
    ui->knobs[k@Name@Gain] = knob;
    cuif_widget_add_child(ui->root, knob);

    return ui;
}

void @lower@UiDestroy(@Name@Ui* ui) {
    if (!ui) return;
    if (ui->root) cuif_widget_destroy(ui->root);
    delete ui;
}

void @lower@UiSyncFromParams(@Name@Ui* ui, const float* params) {
    if (!ui || !params) return;
    for (int i = 0; i < k@Name@NumParams; ++i) {
        if (ui->knobs[i]) ui->knobs[i]->u.knob.value = k@Name@ParamTable.toWidget(i, params[i]);
    }
}
'''

LPI_CPP = r'''/*
 * LPI-ABI build of @Name@ on the platform's wrapper template
 * (PluginABI/wrapper/lpi/plugin_template.h). Everything format-specific
 * lives in the template; this file is only what makes it @Name@.
 */
#include "lpi/plugin_template.h"
#include "@Name@Core.h"
#include "@Name@Ui.h"

struct @Name@LpiTraits {
    using Core = @Name@Core;
    using Ui = @Name@Ui;

    static constexpr lpi_plugin_info kInfo = { "@id@", "@product@", "Loudio", 2, 2 };
    static constexpr int kPanelWidth = k@Name@PanelWidth;
    static constexpr int kPanelHeight = k@Name@PanelHeight;

    static Ui* uiCreate(float w, float h, void* context, loudio::lpi::SendParamsFn sendParams) {
        return @lower@UiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { @lower@UiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { @lower@UiSyncFromParams(ui, params); }
    static void uiBeforeRender(Ui*, Core&) {}
};

LPI_DEFINE_PLUGIN(@Name@LpiTraits)
'''

PROCESSOR_H = r'''#pragma once

#include "LoudioEditor.h" /* VSTs/common: LoudioProcessor + LoudioEditor templates */
#include "@Name@Core.h"
#include "@Name@Ui.h"

/*
 * @Name@'s VST3/Standalone build on the shared JUCE wrapper (VSTs/common).
 * The APVTS layout, rings, cuif child window and DPI handling live in the
 * templates; this is only what makes it @Name@.
 */
struct @Name@VstTraits {
    using Core = @Name@Core;
    using Ui = @Name@Ui;

    static constexpr int kPanelWidth = k@Name@PanelWidth;
    static constexpr int kPanelHeight = k@Name@PanelHeight;
    static constexpr const char* kWindowTitle = "@product@";

    /* Labels for kParamStepped parameters; none in the scaffold. */
    static juce::StringArray choiceLabels(int) { return {}; }

    static Ui* uiCreate(float w, float h, void* context, loudio::vst::SendParamsFn sendParams) {
        return @lower@UiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { @lower@UiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { @lower@UiSyncFromParams(ui, params); }

    /* DSP -> UI frames (meters, analyzers): none in the scaffold. */
    static void afterProcess(Core&, cuif_spsc_ring_buffer&) {}
    static void pollDspToUi(Ui*, cuif_spsc_ring_buffer&) {}
};

using Loudio@Name@Processor = loudio::vst::LoudioProcessor<@Name@VstTraits>;
using Loudio@Name@Editor = loudio::vst::LoudioEditor<@Name@VstTraits>;
'''

PROCESSOR_CPP = r'''#include "PluginProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new Loudio@Name@Processor();
}
'''

DSP_TESTS = r'''/*
 * @Name@ DSP tests: the platform's generic core checks
 * (DSP/include/loudio/testing/CoreTests.h) plus this plugin's own.
 *
 *   dsp_tests                 run everything
 *   dsp_tests --regen-golden  rewrite test/golden/ after an INTENTIONAL DSP change
 */
#include "@Name@Core.h"
#include "loudio/testing/CoreTests.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

LOUDIO_DEFINE_ALLOCATION_HOOK();

#ifndef LOUDIO_@upper@_GOLDEN_DIR
#define LOUDIO_@upper@_GOLDEN_DIR "test/golden"
#endif

namespace {

const char kStateMagic[4] = { '@magic@' };
const int kGoldenIrLength = 8192;
const double kGoldenSampleRate = 44100.0;

/* One golden IR per variant: add one per mode/preset worth pinning. */
const loudio::testing::GoldenVariant<@Name@Core> kGoldenVariants[] = {
    { "default", nullptr },
};
const int kGoldenVariantCount = (int)(sizeof(kGoldenVariants) / sizeof(kGoldenVariants[0]));

/* Plugin-specific: the scaffold core is a pass-through times gain. Replace
 * with real DSP assertions as the plugin grows. */
void test@Name@GainScalesInput() {
    std::cout << "Running test@Name@GainScalesInput..." << std::endl;
    @Name@Core core;
    core.prepare(44100.0, 64);
    core.setParameterValueDirect(k@Name@Gain, 0.5f);
    float l[64], r[64];
    for (int i = 0; i < 64; ++i) { l[i] = 1.0f; r[i] = -1.0f; }
    core.process(l, r, 64);
    for (int i = 0; i < 64; ++i) { assert(std::abs(l[i] - 0.5f) < 1e-6f); assert(std::abs(r[i] + 0.5f) < 1e-6f); }
    std::cout << "  gain = 0.5 halves both channels. Pass." << std::endl;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--regen-golden") {
        std::cout << "Regenerating golden impulse responses..." << std::endl;
        loudio::testing::regenerateGoldenImpulseResponses<@Name@Core>(LOUDIO_@upper@_GOLDEN_DIR, kGoldenVariants, kGoldenVariantCount, kGoldenIrLength, kGoldenSampleRate);
        return 0;
    }

    std::cout << "==============================" << std::endl;
    std::cout << "Starting @product@ DSP Tests" << std::endl;
    std::cout << "==============================" << std::endl;

    using namespace loudio::testing;
    testParamTableIndexIntegrity<@Name@Core>();
    testDefaultsMatchTable<@Name@Core>();
    testDirectSetAppliesImmediately<@Name@Core>(k@Name@Gain, 0.25f);
    testQueuedSetAppliesAfterProcess<@Name@Core>(k@Name@Gain, 0.25f);
    testBatchIsAllOrNothing<@Name@Core>(k@Name@Gain, 0.25f);
    testStateRoundTrip<@Name@Core>(kStateMagic);
    testStateRejectsCorruptBlobs<@Name@Core>();
    testProcessPerformsNoHeapAllocations<@Name@Core>(44100.0, 512, 16, [](@Name@Core& core) {
        core.setParameterValue(k@Name@Gain, 1.5f);
    });
    test@Name@GainScalesInput();
    testImpulseResponseMatchesGolden<@Name@Core>(LOUDIO_@upper@_GOLDEN_DIR, kGoldenVariants, kGoldenVariantCount, kGoldenIrLength, kGoldenSampleRate);

    std::cout << "All @Name@ DSP tests passed successfully!" << std::endl;
    return 0;
}
'''

GUI_HARNESS = r'''/*
 * Host-side test for lpi_@lower@.dll's lpi.gui.offscreen.v1: loads the DLL,
 * opens the offscreen GUI, drags the gain knob, and checks the drag reached
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

enum { kGainIndex = 0 }; /* k@Name@Gain in @Name@Params.h */
/* Knob centre, from k@Name@GainKnob* in @Name@Ui.h. */
static const float kKnobCentreX = 150.0f + 50.0f;
static const float kKnobCentreY = 60.0f + 50.0f;

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
    HMODULE m = LoadLibraryW(L"lpi_@lower@.dll");
    CHECK(m);
    lpi_get_factory_fn getFactory = (lpi_get_factory_fn)GetProcAddress(m, LPI_GET_FACTORY_SYMBOL_NAME);
    CHECK(getFactory);
    const lpi_plugin_factory* factory = getFactory();
    CHECK(factory->abi_version.major == LPI_ABI_VERSION_MAJOR);
    CHECK(strcmp(factory->get_info()->id, "@id@") == 0);
    const lpi_plugin_api* api = factory->get_api();

    lpi_plugin* inst = api->create(48000.0, 512);
    CHECK(inst && api->activate(inst));

    lpi_parameter_info pinfo;
    CHECK(api->get_parameter_info(inst, kGainIndex, &pinfo));
    CHECK(strcmp(pinfo.id, "gain") == 0);

    const lpi_gui_offscreen_v1* gui = (const lpi_gui_offscreen_v1*)api->get_extension(inst, LPI_EXT_GUI_OFFSCREEN_V1);
    CHECK(gui);
    const lpi_param_changes_v1* changes = (const lpi_param_changes_v1*)api->get_extension(inst, LPI_EXT_PARAM_CHANGES_V1);
    CHECK(changes);

    lpi_gui_offscreen_info info;
    CHECK(gui->open(inst, 1.0f, &info));
    CHECK(info.logical_width == @panelW@ && info.logical_height == @panelH@);
    CHECK(info.physical_width == info.logical_width);
    size_t bytes = (size_t)info.physical_width * info.physical_height * 4;
    unsigned char* before = (unsigned char*)malloc(bytes);
    unsigned char* after = (unsigned char*)malloc(bytes);
    CHECK(before && after);

    CHECK(gui->render(inst));
    CHECK(gui->read_pixels(inst, before, bytes));
    writePpm("lpi_@lower@_before.ppm", before, (int)info.physical_width, (int)info.physical_height);

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
    writePpm("lpi_@lower@_after.ppm", after, (int)info.physical_width, (int)info.physical_height);
    CHECK(memcmp(before, after, bytes) != 0);

    gui->close(inst);
    api->deactivate(inst);
    api->destroy(inst);
    free(before);
    free(after);
    FreeLibrary(m);
    printf("LPI @Name@ offscreen GUI harness passed.\n");
    return 0;
}
'''

GOLDEN_README = r'''# Golden impulse responses

Reference IRs for `testImpulseResponseMatchesGolden` in `src/DSPTests.cpp`
(DSP/include/loudio/testing/CoreTests.h). One file per variant in
`kGoldenVariants` (`ir_<name>.f32`): raw little-endian float32, 8192 left
samples then 8192 right samples, unit impulse at 44.1 kHz, every parameter
at its table default unless the variant's `configure` changes it.

`ir_default.f32` was written by tools/new_plugin.py for the scaffold's
pass-through core (an impulse in, an impulse out). After the first real
DSP change, regenerate and commit:

    dsp_tests --regen-golden

The test nulls within 1e-3 (-60 dB), not bit-exact, to absorb compiler
libm differences between the build that produced these and MSVC.
'''

PLUGIN_README = r'''# @product@

Scaffolded by `tools/new_plugin.py @Name@` on the plugin platform (#125).

| Piece | File |
|---|---|
| Parameter table | `src/@Name@Params.h` |
| DSP core (P1 contract) | `src/@Name@Core.{h,cpp}` |
| cuif UI (shared by both editors) | `src/@Name@Ui.{h,cpp}` |
| LPI build (P2 template) | `src/lpi_@lower@.cpp` |
| VST3/Standalone build (P3 templates) | `src/PluginProcessor.{h,cpp}` |
| Tests | `src/DSPTests.cpp` (generic kit + own), `test/lpi_@lower@_gui_harness.c`, `test/golden/` |

Next steps: write the DSP in `@Name@Core::process()`, grow the table,
lay out the UI, add plugin-specific tests, then `dsp_tests --regen-golden`
and commit the new goldens. CI already builds and tests this plugin
(tools/list_plugins.py feeds the matrix).

Build (Windows, from the repo root):

    cmake -S VSTs/@Name@ -B build/@lower@
    cmake --build build/@lower@ --config Debug --target dsp_tests lpi_@lower@ lpi_@lower@_gui_harness lpi_conformance
    cmake --build build/@lower@ --config Debug --target Loudio@Name@_VST3 Loudio@Name@_Standalone

DSP tests only (any platform):

    cmake -S VSTs/@Name@ -B build/@lower@-tests -DLOUDIO_PLUGIN_TESTS_ONLY=ON
    cmake --build build/@lower@-tests --target dsp_tests && ./build/@lower@-tests/dsp_tests
'''


def render(template, tokens):
    out = template
    for k, v in tokens.items():
        out = out.replace("@" + k + "@", str(v))
    leftover = re.search(r"@[A-Za-z]+@", out)
    if leftover:
        sys.exit("unreplaced token " + leftover.group(0))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("name", help="CamelCase plugin name, e.g. Compressor")
    ap.add_argument("--id", help="stable plugin id (default com.loudio.<name>)")
    ap.add_argument("--code", help="4-character VST3 plugin code (default: first 3 letters + '1')")
    ap.add_argument("--product", help='display name (default "Loudio <Name>")')
    ap.add_argument("--force", action="store_true", help="overwrite an existing VSTs/<Name>")
    args = ap.parse_args()

    name = args.name
    if not re.fullmatch(r"[A-Z][A-Za-z0-9]*", name):
        sys.exit("name must be CamelCase starting with a capital letter, e.g. Compressor")
    lower = name.lower()
    code = args.code or (name[:3].capitalize().ljust(3, "x") + "1")
    if len(code) != 4:
        sys.exit("--code must be exactly 4 characters")
    magic = (name[:4].upper()).ljust(4, "_")
    tokens = {
        "Name": name,
        "lower": lower,
        "upper": name.upper(),
        "id": args.id or ("com.loudio." + lower),
        "code": code,
        "product": args.product or ("Loudio " + name),
        "magic": "', '".join(magic),
        "panelW": 400,
        "panelH": 240,
    }

    dest = ROOT / "VSTs" / name
    if dest.exists() and not args.force:
        sys.exit(f"{dest} already exists (use --force to overwrite)")
    if (ROOT / "VSTs" / "common").name == name:
        sys.exit("'common' is reserved")

    files = {
        "CMakeLists.txt": CMAKE,
        "README.md": PLUGIN_README,
        f"src/{name}Params.h": PARAMS_H,
        f"src/{name}Core.h": CORE_H,
        f"src/{name}Core.cpp": CORE_CPP,
        f"src/{name}Ui.h": UI_H,
        f"src/{name}Ui.cpp": UI_CPP,
        f"src/lpi_{lower}.cpp": LPI_CPP,
        "src/PluginProcessor.h": PROCESSOR_H,
        "src/PluginProcessor.cpp": PROCESSOR_CPP,
        "src/DSPTests.cpp": DSP_TESTS,
        f"test/lpi_{lower}_gui_harness.c": GUI_HARNESS,
        "test/golden/README.md": GOLDEN_README,
    }
    for rel, template in files.items():
        path = dest / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(render(template, tokens), newline="\n")

    # Golden IR for the scaffold's pass-through core: impulse in, impulse out
    # on both channels (gain default 1.0). 8192 floats left, 8192 right.
    length = 8192
    channel = struct.pack("<%df" % length, 1.0, *([0.0] * (length - 1)))
    (dest / "test" / "golden" / "ir_default.f32").write_bytes(channel + channel)

    print(f"Scaffolded VSTs/{name} ({tokens['id']}, code {code}, state magic '{magic}')")
    print("Next: see VSTs/%s/README.md. CI picks it up automatically." % name)


if __name__ == "__main__":
    main()
