/*
 * Minimal LPI plugin: a single-parameter linear gain processor. Exists
 * purely to validate the lpi.h ABI shape against a real (trivial) plugin
 * before Reverb's complexity is involved -- see PluginABI's Epic B1.
 *
 * Demonstrates the intended real pattern for RT-safe parameter updates:
 * set_parameter_value() (called from any thread) enqueues into a lock-free
 * SPSC ring buffer; process() (the real-time audio thread) drains it at the
 * top of each block before using the value. This mirrors
 * VSTs/Reverb/src/PluginProcessor.cpp's ParamUpdateMessage/uiToDspRingBuffer
 * pattern exactly.
 */

#include "lpi/lpi.h"
#include "cuif/ring_buffer.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t index;
    float value;
} ToyGainParamMessage;

struct lpi_plugin {
    double sample_rate;
    uint32_t max_block_size;
    bool activated;

    float gain; /* applied value, touched only from process() */

    cuif_spsc_ring_buffer param_ring;
    unsigned char param_storage[64]; /* power of two, ample for a single-parameter plugin */
};

enum { kParamGain = 0, kToyGainNumParams = 1 };

static lpi_plugin* toygain_create(double sample_rate, uint32_t max_block_size) {
    lpi_plugin* instance = (lpi_plugin*)calloc(1, sizeof(lpi_plugin));
    if (!instance) return NULL;

    instance->sample_rate = sample_rate;
    instance->max_block_size = max_block_size;
    instance->gain = 1.0f;
    cuif_spsc_init(&instance->param_ring, instance->param_storage, sizeof(instance->param_storage));

    return instance;
}

static void toygain_destroy(lpi_plugin* instance) {
    free(instance);
}

static bool toygain_activate(lpi_plugin* instance) {
    if (!instance) return false;
    instance->activated = true;
    return true;
}

static void toygain_deactivate(lpi_plugin* instance) {
    if (!instance) return;
    instance->activated = false;
}

static void toygain_process(lpi_plugin* instance, const lpi_process_data* data) {
    if (!instance || !data) return;

    /* Drain pending parameter updates -- real-time thread, never blocks:
     * cuif_spsc_read only ever returns what's already available. */
    ToyGainParamMessage msg;
    while (cuif_spsc_readable(&instance->param_ring) >= sizeof(msg)) {
        if (cuif_spsc_read(&instance->param_ring, (unsigned char*)&msg, sizeof(msg)) != sizeof(msg)) break;
        if (msg.index == kParamGain) instance->gain = msg.value;
    }

    uint32_t channels = data->num_input_channels < data->num_output_channels
        ? data->num_input_channels : data->num_output_channels;

    for (uint32_t ch = 0; ch < channels; ++ch) {
        const float* in = data->inputs[ch];
        float* out = data->outputs[ch];
        for (uint32_t i = 0; i < data->num_frames; ++i) {
            out[i] = in[i] * instance->gain;
        }
    }
    /* Any output channels beyond the input count are left untouched by
     * design -- a toy plugin has no reason to guess what silence/passthrough
     * policy a real host wants there. */
}

static uint32_t toygain_get_parameter_count(lpi_plugin* instance) {
    (void)instance;
    return kToyGainNumParams;
}

static bool toygain_get_parameter_info(lpi_plugin* instance, uint32_t index, lpi_parameter_info* out_info) {
    (void)instance;
    if (index != kParamGain || !out_info) return false;
    out_info->index = kParamGain;
    out_info->id = "gain";
    out_info->name = "Gain";
    out_info->min_value = 0.0f;
    out_info->max_value = 2.0f;
    out_info->default_value = 1.0f;
    out_info->flags = 0;
    return true;
}

static float toygain_get_parameter_value(lpi_plugin* instance, uint32_t index) {
    if (!instance || index != kParamGain) return 0.0f;
    return instance->gain;
}

static bool toygain_set_parameter_value(lpi_plugin* instance, uint32_t index, float value) {
    if (!instance || index != kParamGain) return false;

    /*
     * cuif_spsc_write() is a byte-stream primitive: if fewer bytes are
     * available than requested, it writes a PARTIAL amount rather than
     * refusing outright. That's correct for genuine byte-stream data, but
     * wrong here -- ToyGainParamMessage is a fixed-size record, and a
     * partial write would splice half a message into the ring, permanently
     * desyncing every message boundary the reader assumes from then on.
     * Checking cuif_spsc_writable() first makes this an all-or-nothing
     * write, which is what a fixed-size-record producer actually needs.
     */
    if (cuif_spsc_writable(&instance->param_ring) < sizeof(ToyGainParamMessage)) return false;

    ToyGainParamMessage msg;
    msg.index = index;
    msg.value = value;
    return cuif_spsc_write(&instance->param_ring, (const unsigned char*)&msg, sizeof(msg)) == sizeof(msg);
}

static size_t toygain_get_state(lpi_plugin* instance, void* buffer, size_t buffer_size) {
    if (!instance) return 0;
    if (buffer == NULL) return sizeof(float);
    if (buffer_size < sizeof(float)) return 0;
    memcpy(buffer, &instance->gain, sizeof(float));
    return sizeof(float);
}

static bool toygain_set_state(lpi_plugin* instance, const void* data, size_t size) {
    if (!instance || !data || size < sizeof(float)) return false;
    float gain;
    memcpy(&gain, data, sizeof(float));
    instance->gain = gain;
    return true;
}

static void* toygain_get_extension(lpi_plugin* instance, const char* extension_id) {
    (void)instance;
    (void)extension_id;
    return NULL; /* no extensions implemented -- this plugin only validates the core ABI */
}

static const lpi_plugin_api kToyGainApi = {
    toygain_create,
    toygain_destroy,
    toygain_activate,
    toygain_deactivate,
    toygain_process,
    toygain_get_parameter_count,
    toygain_get_parameter_info,
    toygain_get_parameter_value,
    toygain_set_parameter_value,
    toygain_get_state,
    toygain_set_state,
    toygain_get_extension
};

static const lpi_plugin_info kToyGainInfo = {
    "com.loudio.toygain",
    "Loudio Toy Gain",
    "Loudio",
    2,
    2
};

static const lpi_plugin_info* toygain_get_info(void) {
    return &kToyGainInfo;
}

static const lpi_plugin_api* toygain_get_api(void) {
    return &kToyGainApi;
}

static const lpi_plugin_factory kToyGainFactory = {
    { LPI_ABI_VERSION_MAJOR, LPI_ABI_VERSION_MINOR },
    toygain_get_info,
    toygain_get_api
};

LPI_EXPORT const lpi_plugin_factory* lpi_get_factory(void) {
    return &kToyGainFactory;
}
