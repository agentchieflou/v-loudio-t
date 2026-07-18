/*
 * Minimal host harness: proves the LPI dynamic-loading contract end to end,
 * independent of noprod or any real DAW. Loads a plugin DLL purely via
 * LoadLibrary + GetProcAddress("lpi_get_factory") -- no import library, no
 * compile-time link dependency on the plugin at all, exactly as a real host
 * (noprod's audio_core) will do it later in Epic B5.
 *
 * Plain assert()-based, matching this repo's established test convention
 * (Framework/test/cuif_tests.c, VSTs/Reverb/src/DSPTests.cpp).
 */

#include "lpi/lpi.h"

#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void testLoadAndQueryFactory(HMODULE module, const lpi_plugin_factory** out_factory) {
    printf("Running testLoadAndQueryFactory...\n");

    lpi_get_factory_fn get_factory = (lpi_get_factory_fn)GetProcAddress(module, LPI_GET_FACTORY_SYMBOL_NAME);
    assert(get_factory != NULL);

    const lpi_plugin_factory* factory = get_factory();
    assert(factory != NULL);
    assert(factory->abi_version.major == LPI_ABI_VERSION_MAJOR);
    assert(factory->get_info != NULL);
    assert(factory->get_api != NULL);

    const lpi_plugin_info* info = factory->get_info();
    assert(info != NULL);
    assert(info->id != NULL && strcmp(info->id, "com.loudio.toygain") == 0);
    assert(info->num_audio_inputs == 2);
    assert(info->num_audio_outputs == 2);

    *out_factory = factory;
    printf("  Resolved lpi_get_factory via GetProcAddress, ABI version %u.%u, plugin id '%s'. Pass.\n",
           factory->abi_version.major, factory->abi_version.minor, info->id);
}

static void testParameterInfo(const lpi_plugin_api* api, lpi_plugin* instance) {
    printf("Running testParameterInfo...\n");

    assert(api->get_parameter_count(instance) == 1);

    lpi_parameter_info pinfo;
    memset(&pinfo, 0, sizeof(pinfo));
    assert(api->get_parameter_info(instance, 0, &pinfo));
    assert(strcmp(pinfo.id, "gain") == 0);
    assert(pinfo.default_value == 1.0f);

    /* Out-of-range index must fail cleanly, not read garbage. */
    assert(!api->get_parameter_info(instance, 99, &pinfo));

    printf("  Parameter table exposes exactly the expected 'gain' parameter. Pass.\n");
}

static void testProcessAppliesDefaultGain(const lpi_plugin_api* api, lpi_plugin* instance) {
    printf("Running testProcessAppliesDefaultGain...\n");

    const uint32_t numFrames = 8;
    float inL[8], inR[8], outL[8], outR[8];
    for (uint32_t i = 0; i < numFrames; ++i) { inL[i] = 1.0f; inR[i] = 2.0f; }

    const float* inputs[2] = { inL, inR };
    float* outputs[2] = { outL, outR };

    lpi_process_data data;
    data.inputs = inputs;
    data.outputs = outputs;
    data.num_input_channels = 2;
    data.num_output_channels = 2;
    data.num_frames = numFrames;

    api->process(instance, &data);

    for (uint32_t i = 0; i < numFrames; ++i) {
        assert(outL[i] == inL[i]);   /* default gain is 1.0 -- passthrough */
        assert(outR[i] == inR[i]);
    }

    printf("  process() at default gain (1.0) passes audio through unchanged. Pass.\n");
}

static void testSetParameterValueAppliesOnNextProcess(const lpi_plugin_api* api, lpi_plugin* instance) {
    printf("Running testSetParameterValueAppliesOnNextProcess...\n");

    assert(api->set_parameter_value(instance, 0, 0.5f));

    const uint32_t numFrames = 4;
    float inL[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float outL[4];
    const float* inputs[1] = { inL };
    float* outputs[1] = { outL };

    lpi_process_data data;
    data.inputs = inputs;
    data.outputs = outputs;
    data.num_input_channels = 1;
    data.num_output_channels = 1;
    data.num_frames = numFrames;

    api->process(instance, &data); /* drains the pending gain=0.5 update, then applies it */

    for (uint32_t i = 0; i < numFrames; ++i) {
        assert(outL[i] == 0.5f);
    }
    assert(api->get_parameter_value(instance, 0) == 0.5f);

    /* Out-of-range index must fail cleanly rather than silently accepting garbage. */
    assert(!api->set_parameter_value(instance, 99, 1.0f));

    printf("  set_parameter_value() enqueues, and the value is applied by the next process() call. Pass.\n");
}

static void testStateSaveLoadRoundTrip(const lpi_plugin_api* api, lpi_plugin* instance) {
    printf("Running testStateSaveLoadRoundTrip...\n");

    /* Two-call sizing idiom: query required size, then fill. */
    size_t required = api->get_state(instance, NULL, 0);
    assert(required > 0);

    unsigned char* buffer = (unsigned char*)malloc(required);
    assert(buffer != NULL);
    size_t written = api->get_state(instance, buffer, required);
    assert(written == required);

    lpi_plugin* other = api->create(48000.0, 512);
    assert(other != NULL);
    assert(api->activate(other));

    assert(api->set_state(other, buffer, written));
    assert(api->get_parameter_value(other, 0) == api->get_parameter_value(instance, 0));

    api->deactivate(other);
    api->destroy(other);
    free(buffer);

    printf("  get_state()/set_state() round-trips the current parameter value onto a fresh instance. Pass.\n");
}

static void testGetExtensionReturnsNullForUnknownId(const lpi_plugin_api* api, lpi_plugin* instance) {
    printf("Running testGetExtensionReturnsNullForUnknownId...\n");

    assert(api->get_extension(instance, "lpi.gui.offscreen.v1") == NULL);
    assert(api->get_extension(instance, "not.a.real.extension") == NULL);

    printf("  get_extension() cleanly returns NULL for extensions this plugin doesn't implement. Pass.\n");
}

int main(void) {
    printf("==============================\n");
    printf("Starting LPI Host Harness\n");
    printf("==============================\n");

    HMODULE module = LoadLibraryW(L"lpi_toy_gain.dll");
    if (!module) {
        printf("FATAL: LoadLibraryW(\"lpi_toy_gain.dll\") failed: error %lu\n", GetLastError());
        return 1;
    }

    const lpi_plugin_factory* factory = NULL;
    testLoadAndQueryFactory(module, &factory);

    const lpi_plugin_api* api = factory->get_api();
    assert(api != NULL);

    lpi_plugin* instance = api->create(48000.0, 512);
    assert(instance != NULL);
    assert(api->activate(instance));

    testParameterInfo(api, instance);
    testProcessAppliesDefaultGain(api, instance);
    testSetParameterValueAppliesOnNextProcess(api, instance);
    testStateSaveLoadRoundTrip(api, instance);
    testGetExtensionReturnsNullForUnknownId(api, instance);

    api->deactivate(instance);
    api->destroy(instance);

    FreeLibrary(module);

    printf("All LPI host harness tests passed successfully!\n");
    return 0;
}
