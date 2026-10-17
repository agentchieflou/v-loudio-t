#pragma once

/*
 * Shared VST3 shell on the Steinberg VST3 SDK directly (#171), no JUCE.
 * It replaced the JUCE-based LoudioProcessor.h + LoudioEditor.h (P3, #129)
 * and every plugin's VST3 is built on it (VSTs/Reverb/src/vst3_reverb.cpp
 * shows the shape). It is the VST3
 * counterpart of the LPI wrapper template (PluginABI/wrapper/lpi/
 * plugin_template.h): a plugin's VST3 build is its Traits plus one
 * LOUDIO_VST3_DEFINE_PLUGIN(Traits) line.
 *
 * Shape: the SDK's recommended split. Processor<Traits> is the
 * IComponent/IAudioProcessor (an AudioEffect) and owns the Core;
 * Controller<Traits> is the IEditController and owns the parameter
 * objects and the editor. The host keeps them in sync: edits go out of the
 * controller through beginEdit/performEdit/endEdit and arrive in the
 * processor as IParameterChanges; the processor's state reaches the
 * controller through setComponentState.
 *
 * Parameters come from Core::table() (DSP/include/loudio/plugin/
 * ParamTable.h), in table order:
 *   - ParamID = FNV-1a of the stable id string, top bit cleared (the
 *     host-reserved range), so automation and sessions key on the id,
 *     never on the index -- the same rule the state blob follows;
 *   - kParamBoolean -> stepCount 1; kParamStepped -> stepCount max-min
 *     (a list parameter when Traits::choiceLabels gives one label per
 *     step); anything else is continuous and linear over [min, max];
 *   - kParamReadOnly -> kIsReadOnly, not automatable.
 * The processor keeps every parameter's plain value in an atomic and
 * pushes all of them into the core with setParameterValueDirect() every
 * block, after applying that block's IParameterChanges (last point of each
 * queue: block-accurate, like the LPI and JUCE builds).
 *
 * State is the core's own id-keyed blob (Core::getState/setState,
 * loudio::StateBlob v1), the same bytes the LPI build saves.
 *
 * DSP -> UI data (analyzer frames) goes through an SPSC ring that the
 * processor owns; on connect() the processor sends the controller its
 * address in an IMessage, tagged with a per-process token so a host that
 * runs the two halves in different processes just gets no analyzer, never
 * a bad pointer.
 *
 * Latency (Core::latencySamples(), optional) is reported by
 * getLatencySamples() as of the last setActive(true). A change while
 * processing is flagged to the controller, which calls
 * restartComponent(kLatencyChanged) from the editor's timer; with no
 * editor open it is picked up at the next activation.
 *
 * Editor: an IPlugView hosting the plugin's cuif UI in a native child
 * window. cuif has a Win32/WGL backend only (Framework/), so the view
 * exists on Windows (kPlatformTypeHWND) and createView() returns null
 * elsewhere, where hosts show their generic parameter editor.
 * LOUDIO_VST3_EDITOR is defined on platforms that have it.
 *
 * Traits contract:
 *   using Core = ...;                                   // CORE_CONTRACT.md
 *   static constexpr const char* kName;                 // "Loudio Reverb"
 *   static constexpr const char* kVendor, *kUrl, *kEmail;
 *   static constexpr const char* kCategories;           // "Fx|Reverb"
 *   static constexpr Steinberg::uint32 kProcessorUid[4], kControllerUid[4];
 *   static std::vector<const char*> choiceLabels(int index);  // kParamStepped entries; empty = numeric labels
 *   static void afterProcess(Core& core, cuif_spsc_ring_buffer& dspToUi);  // audio thread; may be empty
 *   static Steinberg::uint32 tailSamples(const float* values, double sampleRate);  // optional: getTailSamples()
 * and, with LOUDIO_VST3_EDITOR:
 *   using Ui = ...;
 *   static constexpr int kPanelWidth, kPanelHeight;     // logical px
 *   static constexpr const char* kWindowTitle;
 *   static Ui*  uiCreate(float w, float h, void* context, loudio::vst3::SendParamsFn sendParams);
 *   static void uiDestroy(Ui* ui);                      // safe on nullptr
 *   static cuif_widget* uiRoot(Ui* ui);
 *   static void uiSyncFromParams(Ui* ui, const float* params);   // never calls sendParams
 *   static void pollDspToUi(Ui* ui, cuif_spsc_ring_buffer& fromDsp);  // UI thread
 */

#include "loudio/plugin/ParamTable.h"
#include "loudio/plugin/Transport.h"
#include "cuif/ring_buffer.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#ifdef LOUDIO_VST3_EDITOR
#include "cuif/cuif.h"
#include "public.sdk/source/common/pluginview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include <windows.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace loudio {
namespace vst3 {

namespace sv = Steinberg::Vst;
using Steinberg::tresult;
using Steinberg::kResultOk;
using Steinberg::kResultTrue;
using Steinberg::kResultFalse;
using Steinberg::kInvalidArgument;

typedef void (*SendParamsFn)(void* context, const ParamUpdate* updates, size_t count);

/* An analyzer/meter frame on the DSP -> UI ring: one tag byte, then N
 * floats. All-or-nothing so a reader never sees a tag without its payload. */
template <size_t N>
inline void pushFrame(cuif_spsc_ring_buffer& rb, unsigned char tag, const float (&values)[N]) {
    if (cuif_spsc_writable(&rb) < sizeof(tag) + sizeof(values)) return;
    cuif_spsc_write(&rb, &tag, sizeof(tag));
    cuif_spsc_write(&rb, (const unsigned char*)values, sizeof(values));
}

/* ---- Parameter mapping (shared by processor, controller and tests) ---- */

/* FNV-1a, top bit cleared: VST3 reserves ParamIDs >= 0x80000000 for hosts. */
inline sv::ParamID paramIdFor(const char* id) {
    uint32_t h = 2166136261u;
    for (const unsigned char* p = (const unsigned char*)id; *p; ++p) {
        h ^= *p;
        h *= 16777619u;
    }
    return (sv::ParamID)(h & 0x7FFFFFFFu);
}

inline int stepCountFor(const ParamMeta& m) {
    if (m.flags & kParamBoolean) return 1;
    if (m.flags & kParamStepped) return (int)std::lround(m.maxValue - m.minValue);
    return 0;
}

inline double toNormalized(const ParamMeta& m, double plain) {
    const double range = (double)m.maxValue - (double)m.minValue;
    if (range <= 0.0) return 0.0;
    if (m.flags & kParamBoolean) return plain >= 0.5 * ((double)m.minValue + (double)m.maxValue) ? 1.0 : 0.0;
    if (m.flags & kParamStepped) {
        const int steps = stepCountFor(m);
        double s = std::round(plain - (double)m.minValue);
        s = std::clamp(s, 0.0, (double)steps);
        return steps > 0 ? s / steps : 0.0;
    }
    return std::clamp((plain - (double)m.minValue) / range, 0.0, 1.0);
}

inline float toPlain(const ParamMeta& m, double normalized) {
    normalized = std::clamp(normalized, 0.0, 1.0);
    if (m.flags & kParamBoolean) return normalized >= 0.5 ? m.maxValue : m.minValue;
    if (m.flags & kParamStepped) {
        const int steps = stepCountFor(m);
        return (float)((double)m.minValue + std::round(normalized * steps));
    }
    return (float)((double)m.minValue + normalized * ((double)m.maxValue - (double)m.minValue));
}

/* Decimal places for display, from the step hint (0.1 -> 1, 0.01 -> 2). */
inline int precisionFor(const ParamMeta& m) {
    if (m.flags & (kParamBoolean | kParamStepped)) return 0;
    int digits = 0;
    for (double s = m.stepValue; s > 0.0 && s < 0.999 && digits < 6; s *= 10.0) ++digits;
    return digits;
}

/* ParamID -> table index, built once per instance (not on the audio
 * thread). Linear search: tables are tens of entries. */
struct ParamIndex {
    std::vector<sv::ParamID> ids;

    explicit ParamIndex(const ParamTable& table) {
        ids.reserve((size_t)table.count);
        for (int i = 0; i < table.count; ++i) ids.push_back(paramIdFor(table[i].id));
    }
    int find(sv::ParamID id) const {
        for (size_t i = 0; i < ids.size(); ++i) if (ids[i] == id) return (int)i;
        return -1;
    }
    /* False if two ids hash to the same ParamID (checked by the tests). */
    bool unique() const {
        for (size_t i = 0; i < ids.size(); ++i)
            for (size_t j = i + 1; j < ids.size(); ++j)
                if (ids[i] == ids[j]) return false;
        return true;
    }
};

/* Reads a whole IBStream into memory (state is small; capped at 1 MiB). */
inline bool readAll(Steinberg::IBStream* stream, std::vector<unsigned char>& out) {
    if (!stream) return false;
    out.clear();
    unsigned char chunk[4096];
    for (;;) {
        Steinberg::int32 got = 0;
        if (stream->read(chunk, (Steinberg::int32)sizeof(chunk), &got) != kResultOk) break;
        if (got <= 0) break;
        out.insert(out.end(), chunk, chunk + got);
        if (out.size() > (1u << 20)) return false;
        if (got < (Steinberg::int32)sizeof(chunk)) break;
    }
    return true;
}

/* Identifies this process, so the processor <-> controller pointer link
 * is only taken when both halves really live in the same address space. */
inline Steinberg::int64 processToken() {
    static const Steinberg::int64 token =
        (Steinberg::int64)std::chrono::steady_clock::now().time_since_epoch().count() ^ (Steinberg::int64)(intptr_t)&token;
    return token;
}

constexpr const char* kLinkMessageId = "loudio.dspLink";

/* What the processor shares with an in-process controller. */
struct DspLink {
    cuif_spsc_ring_buffer dspToUi;
    unsigned char dspToUiStorage[1024];
    std::atomic<Steinberg::uint32> latency { 0 };
    std::atomic<bool> latencyChanged { false };

    DspLink() { cuif_spsc_init(&dspToUi, dspToUiStorage, sizeof(dspToUiStorage)); }
};

template <class Core>
Steinberg::uint32 coreLatency(const Core& core) {
    if constexpr (requires { core.latencySamples(); }) {
        return (Steinberg::uint32)std::max(0, (int)core.latencySamples());
    } else {
        (void)core;
        return 0;
    }
}

/* ======================================================================
 * Processor
 * ====================================================================== */
template <class Traits>
class Processor : public sv::AudioEffect {
public:
    using Core = typename Traits::Core;

    Processor() : paramIndex(Core::table()) {
        const ParamTable& table = Core::table();
        paramCount = table.count;
        values.reset(new std::atomic<float>[(size_t)paramCount]);
        for (int i = 0; i < paramCount; ++i) values[(size_t)i].store(table[i].defaultValue);
        setControllerClass(Steinberg::FUID(Traits::kControllerUid[0], Traits::kControllerUid[1],
                                           Traits::kControllerUid[2], Traits::kControllerUid[3]));
        if constexpr (requires (Core& c, const TransportInfo& t) { c.setTransport(t); }) {
            processContextRequirements.needTempo().needProjectTimeMusic().needTransportState();
        }
    }

    static Steinberg::FUnknown* createInstance(void*) { return (sv::IAudioProcessor*)new Processor(); }

    tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE {
        tresult result = AudioEffect::initialize(context);
        if (result != kResultOk) return result;
        addAudioInput(STR16("Stereo In"), sv::SpeakerArr::kStereo);
        addAudioOutput(STR16("Stereo Out"), sv::SpeakerArr::kStereo);
        return kResultOk;
    }

    /* Stereo in, stereo out: the only layout the cores process. */
    tresult PLUGIN_API setBusArrangements(sv::SpeakerArrangement* inputs, Steinberg::int32 numIns,
                                          sv::SpeakerArrangement* outputs, Steinberg::int32 numOuts) SMTG_OVERRIDE {
        if (numIns != 1 || numOuts != 1 || !inputs || !outputs) return kResultFalse;
        if (inputs[0] != sv::SpeakerArr::kStereo || outputs[0] != sv::SpeakerArr::kStereo) return kResultFalse;
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    }

    tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 size) SMTG_OVERRIDE {
        return size == sv::kSample32 ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API setupProcessing(sv::ProcessSetup& setup) SMTG_OVERRIDE {
        if (setup.symbolicSampleSize != sv::kSample32) return kResultFalse;
        return AudioEffect::setupProcessing(setup);
    }

    tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE {
        if (state) {
            /* Current values first so prepare() sizes anything that depends
             * on a parameter for what is set now (as the JUCE build did). */
            pushValuesIntoCore();
            maxBlock = std::max<Steinberg::int32>(1, processSetup.maxSamplesPerBlock);
            core.prepare(processSetup.sampleRate, (int)maxBlock);
            link.latency.store(coreLatency(core));
            link.latencyChanged.store(false);
        }
        return AudioEffect::setActive(state);
    }

    Steinberg::uint32 PLUGIN_API getLatencySamples() SMTG_OVERRIDE { return link.latency.load(); }

    /* How long output can outlast input (a reverb's decay), from the
     * optional Traits::tailSamples(values, sampleRate); none by default. */
    Steinberg::uint32 PLUGIN_API getTailSamples() SMTG_OVERRIDE {
        if constexpr (requires (const float* v, double sr) { Traits::tailSamples(v, sr); }) {
            std::vector<float> current((size_t)paramCount);
            for (int i = 0; i < paramCount; ++i) current[(size_t)i] = values[(size_t)i].load();
            return Traits::tailSamples(current.data(), processSetup.sampleRate);
        } else {
            return sv::kNoTail;
        }
    }

    tresult PLUGIN_API process(sv::ProcessData& data) SMTG_OVERRIDE {
        applyParameterChanges(data.inputParameterChanges);

        /* A flush call (no audio) only carries parameter changes. */
        if (data.numSamples <= 0 || data.numOutputs < 1 || data.outputs[0].numChannels < 2) return kResultOk;
        if (data.symbolicSampleSize != sv::kSample32) return kResultFalse;

        pushValuesIntoCore();
        /* Only from a host that sends a process context; without one the
         * core keeps free-running, as in the LPI build with no
         * lpi.transport.v1 calls (CORE_CONTRACT.md). */
        if constexpr (requires (Core& c, const TransportInfo& t) { c.setTransport(t); }) {
            if (data.processContext) core.setTransport(transportFrom(data.processContext));
        }

        float* outL = data.outputs[0].channelBuffers32[0];
        float* outR = data.outputs[0].channelBuffers32[1];
        const float* inL = nullptr;
        const float* inR = nullptr;
        if (data.numInputs > 0 && data.inputs[0].numChannels > 0) {
            inL = data.inputs[0].channelBuffers32[0];
            inR = data.inputs[0].numChannels > 1 ? data.inputs[0].channelBuffers32[1] : inL;
        }
        const size_t bytes = sizeof(float) * (size_t)data.numSamples;
        if (inL != outL) { if (inL) std::memmove(outL, inL, bytes); else std::memset(outL, 0, bytes); }
        if (inR != outR) { if (inR) std::memmove(outR, inR, bytes); else std::memset(outR, 0, bytes); }

        /* Never hand the core more than it was prepared for. */
        for (Steinberg::int32 done = 0; done < data.numSamples;) {
            const Steinberg::int32 n = std::min(maxBlock, data.numSamples - done);
            core.process(outL + done, outR + done, (int)n);
            done += n;
        }
        data.outputs[0].silenceFlags = 0;

        Traits::afterProcess(core, link.dspToUi);

        const Steinberg::uint32 latency = coreLatency(core);
        if (latency != link.latency.load(std::memory_order_relaxed)) {
            link.latency.store(latency);
            link.latencyChanged.store(true);
        }
        return kResultOk;
    }

    /* Main thread. The blob is built from the current values through a
     * shadow core, so it is the core's own format, byte for byte. */
    tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE {
        if (!state) return kInvalidArgument;
        for (int i = 0; i < paramCount; ++i) shadow.setParameterValueDirect(i, values[(size_t)i].load());
        std::vector<unsigned char> blob(shadow.getState(nullptr, 0));
        if (blob.empty() || shadow.getState(blob.data(), blob.size()) != blob.size()) return kResultFalse;
        Steinberg::int32 written = 0;
        return state->write(blob.data(), (Steinberg::int32)blob.size(), &written) == kResultOk
                && written == (Steinberg::int32)blob.size() ? kResultOk : kResultFalse;
    }

    /* Main thread, possibly while processing: decode into the shadow core,
     * then publish through the atomics the audio thread reads. A blob the
     * core rejects leaves every value untouched. */
    tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE {
        std::vector<unsigned char> blob;
        if (!readAll(state, blob)) return kResultFalse;
        if (!shadow.setState(blob.data(), blob.size())) return kResultFalse;
        for (int i = 0; i < paramCount; ++i) values[(size_t)i].store(shadow.getParameterValue(i));
        return kResultOk;
    }

    tresult PLUGIN_API connect(sv::IConnectionPoint* other) SMTG_OVERRIDE {
        tresult result = AudioEffect::connect(other);
        if (result == kResultTrue || result == kResultOk) {
            if (Steinberg::IPtr<sv::IMessage> msg = Steinberg::owned(allocateMessage())) {
                msg->setMessageID(kLinkMessageId);
                msg->getAttributes()->setInt("link", (Steinberg::int64)(intptr_t)&link);
                msg->getAttributes()->setInt("token", processToken());
                sendMessage(msg);
            }
        }
        return result;
    }

    /* Plain value of a parameter as the processor holds it (tests). */
    float valueAt(int index) const { return values[(size_t)index].load(); }

private:
    void applyParameterChanges(sv::IParameterChanges* changes) {
        if (!changes) return;
        const ParamTable& table = Core::table();
        const Steinberg::int32 count = changes->getParameterCount();
        for (Steinberg::int32 i = 0; i < count; ++i) {
            sv::IParamValueQueue* queue = changes->getParameterData(i);
            if (!queue) continue;
            const int index = paramIndex.find(queue->getParameterId());
            if (index < 0) continue;
            const Steinberg::int32 points = queue->getPointCount();
            if (points <= 0) continue;
            Steinberg::int32 offset = 0;
            sv::ParamValue normalized = 0.0;
            if (queue->getPoint(points - 1, offset, normalized) != kResultTrue) continue;
            values[(size_t)index].store(toPlain(table[index], normalized));
        }
    }

    void pushValuesIntoCore() {
        for (int i = 0; i < paramCount; ++i) core.setParameterValueDirect(i, values[(size_t)i].load());
    }

    static TransportInfo transportFrom(const sv::ProcessContext* ctx) {
        TransportInfo t;
        t.sampleRate = ctx->sampleRate;
        if (ctx->state & sv::ProcessContext::kTempoValid) { t.tempoBpm = ctx->tempo; t.flags |= kTransportTempoValid; }
        if (ctx->state & sv::ProcessContext::kProjectTimeMusicValid) { t.ppqPosition = ctx->projectTimeMusic; t.flags |= kTransportPpqValid; }
        if (ctx->state & sv::ProcessContext::kPlaying) t.flags |= kTransportPlaying;
        if (ctx->state & sv::ProcessContext::kCycleActive) t.flags |= kTransportLooping;
        return t;
    }

    Core core;
    Core shadow; /* main thread only: state encode/decode */
    ParamIndex paramIndex;
    int paramCount = 0;
    std::unique_ptr<std::atomic<float>[]> values;
    Steinberg::int32 maxBlock = 1024;
    DspLink link;
};

/* ======================================================================
 * Controller
 * ====================================================================== */

/* One table entry as a VST3 parameter: plain <-> normalized and text. */
template <class Traits>
class TableParameter : public sv::Parameter {
public:
    explicit TableParameter(int index) : index(index), meta(Traits::Core::table()[index]) {
        labels = Traits::choiceLabels(index);
        if ((meta.flags & kParamStepped) && (int)labels.size() != stepCountFor(meta) + 1) labels.clear();

        sv::StringConvert::convert(meta.name, info.title);
        sv::StringConvert::convert(meta.name, info.shortTitle);
        info.units[0] = 0;
        info.id = paramIdFor(meta.id);
        info.stepCount = stepCountFor(meta);
        info.defaultNormalizedValue = vst3::toNormalized(meta, meta.defaultValue);
        info.unitId = sv::kRootUnitId;
        if (meta.flags & kParamReadOnly) {
            info.flags = sv::ParameterInfo::kIsReadOnly;
        } else {
            info.flags = sv::ParameterInfo::kCanAutomate;
            if (!labels.empty()) info.flags |= sv::ParameterInfo::kIsList;
        }
        valueNormalized = info.defaultNormalizedValue;
        precision = precisionFor(meta);
    }

    sv::ParamValue toPlain(sv::ParamValue normalized) const SMTG_OVERRIDE { return vst3::toPlain(meta, normalized); }
    sv::ParamValue toNormalized(sv::ParamValue plain) const SMTG_OVERRIDE { return vst3::toNormalized(meta, plain); }

    void toString(sv::ParamValue normalized, sv::String128 string) const SMTG_OVERRIDE {
        const float plain = vst3::toPlain(meta, normalized);
        char text[64];
        if (!labels.empty()) {
            std::snprintf(text, sizeof(text), "%s", labels[(size_t)std::lround(plain - meta.minValue)]);
        } else if (meta.flags & kParamBoolean) {
            std::snprintf(text, sizeof(text), "%s", plain >= 0.5f ? "On" : "Off");
        } else {
            std::snprintf(text, sizeof(text), "%.*f", precision, (double)plain);
        }
        sv::StringConvert::convert(text, string);
    }

    bool fromString(const sv::TChar* string, sv::ParamValue& normalized) const SMTG_OVERRIDE {
        const std::string text = sv::StringConvert::convert(string);
        for (size_t i = 0; i < labels.size(); ++i) {
            if (text == labels[i]) { normalized = vst3::toNormalized(meta, meta.minValue + (double)i); return true; }
        }
        if (meta.flags & kParamBoolean) {
            if (text == "On") { normalized = 1.0; return true; }
            if (text == "Off") { normalized = 0.0; return true; }
        }
        char* end = nullptr;
        const double plain = std::strtod(text.c_str(), &end);
        if (end == text.c_str()) return false;
        normalized = vst3::toNormalized(meta, plain);
        return true;
    }

private:
    int index;
    ParamMeta meta;
    std::vector<const char*> labels;
};

#ifdef LOUDIO_VST3_EDITOR
template <class Traits> class View;
#endif

template <class Traits>
class Controller : public sv::EditController {
public:
    using Core = typename Traits::Core;

    Controller() : paramIndex(Core::table()) {}

    static Steinberg::FUnknown* createInstance(void*) { return (sv::IEditController*)new Controller(); }

    tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE {
        tresult result = EditController::initialize(context);
        if (result != kResultOk) return result;
        const ParamTable& table = Core::table();
        for (int i = 0; i < table.count; ++i) parameters.addParameter(new TableParameter<Traits>(i));
        touched.assign((size_t)table.count, 0);
        return kResultOk;
    }

    tresult PLUGIN_API terminate() SMTG_OVERRIDE {
        link = nullptr;
        return EditController::terminate();
    }

    /* The processor's blob, as its getState() wrote it. */
    tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE {
        std::vector<unsigned char> blob;
        if (!readAll(state, blob)) return kResultFalse;
        if (!shadow.setState(blob.data(), blob.size())) return kResultFalse;
        const ParamTable& table = Core::table();
        for (int i = 0; i < table.count; ++i) {
            setParamNormalized(paramIndex.ids[(size_t)i], toNormalized(table[i], shadow.getParameterValue(i)));
        }
        return kResultOk;
    }

    tresult PLUGIN_API notify(sv::IMessage* message) SMTG_OVERRIDE {
        if (message && std::strcmp(message->getMessageID(), kLinkMessageId) == 0) {
            Steinberg::int64 address = 0, token = 0;
            if (message->getAttributes()->getInt("link", address) == kResultOk
                && message->getAttributes()->getInt("token", token) == kResultOk
                && token == processToken()) {
                link = (DspLink*)(intptr_t)address;
            }
            return kResultOk;
        }
        return EditController::notify(message);
    }

    tresult PLUGIN_API disconnect(sv::IConnectionPoint* other) SMTG_OVERRIDE {
        link = nullptr;
        return EditController::disconnect(other);
    }

    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE {
#ifdef LOUDIO_VST3_EDITOR
        if (name && std::strcmp(name, sv::ViewType::kEditor) == 0) return new View<Traits>(this);
#endif
        (void)name;
        return nullptr;
    }

    /* Current plain values, table order (the editor's view of the host). */
    void readValues(std::vector<float>& out) {
        const ParamTable& table = Core::table();
        out.resize((size_t)table.count);
        for (int i = 0; i < table.count; ++i) {
            out[(size_t)i] = toPlain(table[i], getParamNormalized(paramIndex.ids[(size_t)i]));
        }
    }

    /* Edits from the UI. Inside a gesture (a widget is being dragged) each
     * parameter's beginEdit is sent once and endEdit at endGesture(), so
     * the host records one touch per drag; otherwise every edit is a
     * complete begin/perform/end. Related edits (a curve's nodes) are one
     * group edit. */
    void uiEdit(const ParamUpdate* updates, size_t count, bool inGesture) {
        const ParamTable& table = Core::table();
        if (count > 1) startGroupEdit();
        for (size_t i = 0; i < count; ++i) {
            const int index = updates[i].index;
            if (!table.valid(index) || (table[index].flags & kParamReadOnly)) continue;
            const sv::ParamID id = paramIndex.ids[(size_t)index];
            const sv::ParamValue normalized = toNormalized(table[index], table.sanitize(index, updates[i].value));
            if (!inGesture) beginEdit(id);
            else if (!touched[(size_t)index]) { touched[(size_t)index] = 1; beginEdit(id); }
            setParamNormalized(id, normalized);
            performEdit(id, normalized);
            if (!inGesture) endEdit(id);
        }
        if (count > 1) finishGroupEdit();
    }

    void endGesture() {
        for (size_t i = 0; i < touched.size(); ++i) {
            if (!touched[i]) continue;
            touched[i] = 0;
            endEdit(paramIndex.ids[i]);
        }
    }

    /* UI thread: report a latency change the processor flagged. */
    void pollLatency() {
        if (link && link->latencyChanged.exchange(false) && componentHandler) {
            componentHandler->restartComponent(sv::kLatencyChanged);
        }
    }

    DspLink* dspLink() const { return link; }
    const ParamIndex& index() const { return paramIndex; }

private:
    Core shadow; /* decodes setComponentState blobs */
    ParamIndex paramIndex;
    std::vector<unsigned char> touched;
    DspLink* link = nullptr;
};

/* ======================================================================
 * Editor (Win32 only: cuif's one window backend)
 * ====================================================================== */
#ifdef LOUDIO_VST3_EDITOR

constexpr const char* kFontPath = "C:\\Windows\\Fonts\\arial.ttf";
constexpr float kFontDesignSizePx = 13.0f;

/* Every open editor is driven by one ~60 Hz thread timer: the host owns
 * the message loop, so the view never pumps messages itself. */
struct EditorTimer {
    struct Entry { void* view; void (*tick)(void*); };
    static std::vector<Entry>& entries() { static std::vector<Entry> e; return e; }
    static UINT_PTR& id() { static UINT_PTR i = 0; return i; }

    static void CALLBACK onTimer(HWND, UINT, UINT_PTR, DWORD) {
        std::vector<Entry> copy = entries(); /* a tick may close a view */
        for (const Entry& e : copy) e.tick(e.view);
    }
    static void add(void* view, void (*tick)(void*)) {
        entries().push_back({ view, tick });
        if (id() == 0) id() = SetTimer(nullptr, 0, 16, &EditorTimer::onTimer);
    }
    static void remove(void* view) {
        auto& e = entries();
        e.erase(std::remove_if(e.begin(), e.end(), [view](const Entry& x) { return x.view == view; }), e.end());
        if (e.empty() && id() != 0) { KillTimer(nullptr, id()); id() = 0; }
    }
};

template <class Traits>
class View : public Steinberg::CPluginView, public Steinberg::IPlugViewContentScaleSupport {
public:
    using Ui = typename Traits::Ui;

    explicit View(Controller<Traits>* controller) : controller(controller) {
        controller->addRef();
        updateRect();
    }
    ~View() override {
        destroyWindow();
        controller->release();
    }

    tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) SMTG_OVERRIDE {
        return type && std::strcmp(type, Steinberg::kPlatformTypeHWND) == 0 ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) SMTG_OVERRIDE {
        if (isPlatformTypeSupported(type) != kResultTrue || !parent) return kResultFalse;
        if (CPluginView::attached(parent, type) != kResultOk) return kResultFalse;

        cuif_window_desc desc = { 0 };
        desc.title = Traits::kWindowTitle;
        desc.width = Traits::kPanelWidth;
        desc.height = Traits::kPanelHeight;
        desc.parent_native_handle = parent;
        desc.dpi_scale = scale;
        window = cuif_window_create(&desc);
        if (!window) return kResultFalse;

        /* Loaded once per process and kept (as the JUCE build did): every
         * editor's GL context shares cuif's root context. */
        if (cuif_global_font == nullptr) {
            cuif_global_font = cuif_font_load(kFontPath, kFontDesignSizePx);
        }
        rebakeFont();

        ui = Traits::uiCreate((float)Traits::kPanelWidth, (float)Traits::kPanelHeight, this, &View::sendParams);
        if (ui) {
            cuif_window_set_root_widget(window, Traits::uiRoot(ui));
            syncFromController(true);
        }
        EditorTimer::add(this, &View::tick);
        return kResultOk;
    }

    tresult PLUGIN_API removed() SMTG_OVERRIDE {
        destroyWindow();
        return CPluginView::removed();
    }

    tresult PLUGIN_API getSize(Steinberg::ViewRect* size) SMTG_OVERRIDE {
        if (!size) return kInvalidArgument;
        *size = rect;
        return kResultTrue;
    }

    tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) SMTG_OVERRIDE {
        if (!newSize) return kInvalidArgument;
        rect = *newSize;
        return kResultTrue;
    }

    /* Windows passes physical pixels; this is the host's DPI scale. */
    tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) SMTG_OVERRIDE {
        if (factor <= 0.0f) factor = 1.0f;
        if (std::fabs(factor - scale) < 0.001f) return kResultTrue;
        scale = factor;
        updateRect();
        if (window) {
            cuif_window_set_dpi_scale(window, scale);
            rebakeFont();
        }
        if (plugFrame) plugFrame->resizeView(this, &rect);
        return kResultTrue;
    }

    /* FUnknown: CPluginView's own interfaces plus the scale support. */
    tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) SMTG_OVERRIDE {
        QUERY_INTERFACE(iid, obj, Steinberg::IPlugViewContentScaleSupport::iid, Steinberg::IPlugViewContentScaleSupport)
        return CPluginView::queryInterface(iid, obj);
    }
    REFCOUNT_METHODS(CPluginView)

private:
    void updateRect() {
        rect.left = 0;
        rect.top = 0;
        rect.right = (Steinberg::int32)std::lround(Traits::kPanelWidth * scale);
        rect.bottom = (Steinberg::int32)std::lround(Traits::kPanelHeight * scale);
    }

    void rebakeFont() {
        if (cuif_global_font && bakedScale != scale) {
            cuif_font_rebake(cuif_global_font, cuif_font_effective_bake_px(kFontDesignSizePx, scale));
            bakedScale = scale;
        }
    }

    void destroyWindow() {
        EditorTimer::remove(this);
        if (gestureOpen) { controller->endGesture(); gestureOpen = false; }
        if (window) cuif_window_set_root_widget(window, nullptr);
        Traits::uiDestroy(ui);
        ui = nullptr;
        if (window) cuif_window_destroy(window);
        window = nullptr;
    }

    /* Push host-side values into the widgets when they changed for a
     * reason other than this UI (automation, a preset, the generic
     * editor). Values the UI sent itself are already in the snapshot. */
    void syncFromController(bool force) {
        controller->readValues(current);
        bool changed = force || current.size() != snapshot.size();
        if (!changed) {
            const ParamTable& table = Traits::Core::table();
            for (size_t i = 0; i < current.size(); ++i) {
                const float tolerance = 1e-5f * (table[(int)i].maxValue - table[(int)i].minValue);
                if (std::fabs(current[i] - snapshot[i]) > tolerance) { changed = true; break; }
            }
        }
        if (!changed) return;
        snapshot = current;
        Traits::uiSyncFromParams(ui, snapshot.data());
    }

    static void sendParams(void* context, const ParamUpdate* updates, size_t count) {
        View* self = static_cast<View*>(context);
        const bool dragging = self->window && cuif_window_get_active_widget(self->window) != nullptr;
        self->controller->uiEdit(updates, count, dragging);
        if (dragging) self->gestureOpen = true;
        const ParamTable& table = Traits::Core::table();
        if (self->snapshot.size() == (size_t)table.count) {
            for (size_t i = 0; i < count; ++i) {
                if (table.valid(updates[i].index)) {
                    self->snapshot[(size_t)updates[i].index] = table.sanitize(updates[i].index, updates[i].value);
                }
            }
        }
    }

    static void tick(void* context) {
        View* self = static_cast<View*>(context);
        if (!self->window) return;
        /* The drag that opened a gesture has ended: release it. */
        if (self->gestureOpen && cuif_window_get_active_widget(self->window) == nullptr) {
            self->controller->endGesture();
            self->gestureOpen = false;
        }
        self->controller->pollLatency();
        if (self->ui) {
            if (!self->gestureOpen) self->syncFromController(false);
            if (DspLink* link = self->controller->dspLink()) Traits::pollDspToUi(self->ui, link->dspToUi);
        }
        cuif_window_render_frame(self->window);
    }

    Controller<Traits>* controller;
    cuif_window* window = nullptr;
    Ui* ui = nullptr;
    float scale = 1.0f;
    float bakedScale = 1.0f;
    bool gestureOpen = false;
    std::vector<float> snapshot, current;
};

#endif /* LOUDIO_VST3_EDITOR */

} // namespace vst3
} // namespace loudio

/* The module's factory: the processor and its controller. Use once, at
 * namespace scope, in the plugin's vst3_<name>.cpp. LOUDIO_PLUGIN_VERSION
 * comes from the plugin's CMake project version. */
#define LOUDIO_VST3_DEFINE_PLUGIN(Traits) \
    BEGIN_FACTORY(Traits::kVendor, Traits::kUrl, Traits::kEmail, PFactoryInfo::kUnicode) \
    DEF_CLASS2(INLINE_UID(Traits::kProcessorUid[0], Traits::kProcessorUid[1], Traits::kProcessorUid[2], Traits::kProcessorUid[3]), \
               PClassInfo::kManyInstances, kVstAudioEffectClass, Traits::kName, 0, Traits::kCategories, \
               LOUDIO_PLUGIN_VERSION, kVstVersionString, loudio::vst3::Processor<Traits>::createInstance) \
    DEF_CLASS2(INLINE_UID(Traits::kControllerUid[0], Traits::kControllerUid[1], Traits::kControllerUid[2], Traits::kControllerUid[3]), \
               PClassInfo::kManyInstances, kVstComponentControllerClass, Traits::kName, 0, "", \
               LOUDIO_PLUGIN_VERSION, kVstVersionString, loudio::vst3::Controller<Traits>::createInstance) \
    END_FACTORY
