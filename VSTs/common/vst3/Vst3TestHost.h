#pragma once

/*
 * Test kit for a plugin's VST3 build on the shared shell (#171): a minimal
 * host around the built bundle, loaded the way a host loads it
 * (VST3::Hosting::Module, the SDK's hosting classes), and the checks
 * every plugin gets from runVst3CoreTests<Core>():
 *
 *   - factory: one processor and one controller class, name, version,
 *     subcategories;
 *   - parameters: the table's entries in order, ParamID = hash of the id,
 *     step counts, defaults, plain <-> normalized round trips, labels;
 *   - buses: stereo in/out, other arrangements refused, 32-bit only;
 *   - latency: the core's latencySamples() after prepare;
 *   - same sound: the bundle's output equals the core run directly (and,
 *     given lpi_<name>.dll on Windows, the LPI build) for the same input
 *     and parameters, set in block 0 and moved again mid-stream;
 *   - state: getState is the core's blob; a fresh instance restores the
 *     values, the bytes and the sound; a corrupt blob changes nothing.
 *
 * A plugin's test/vst3_<name>_tests.cpp is its own checks plus one call.
 */
#include "vst3/LoudioVst3.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"

#ifdef _WIN32
#include "lpi/lpi.h"
#include <windows.h>
#endif

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace loudio {
namespace vst3test {

namespace sv = Steinberg::Vst;
using Steinberg::IPtr;
using Steinberg::kResultOk;
using Steinberg::kResultTrue;
using Steinberg::kResultFalse;

inline int& failures() { static int n = 0; return n; }

#define LOUDIO_CHECK(cond) do { if (!(cond)) { std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++loudio::vst3test::failures(); } } while (0)

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 256;
constexpr int kNumBlocks = 375; /* 2 s */
constexpr int kChangeBlock = 150;
constexpr float kTolerance = 1e-6f;

/* (table index, normalized value) */
using Changes = std::vector<std::pair<int, double>>;

/* A stereo test signal: an impulse on each side, a noise burst and two
 * tones, about -12 dBFS. */
inline void makeInput(std::vector<float>& l, std::vector<float>& r) {
    const size_t n = (size_t)kBlock * kNumBlocks;
    l.assign(n, 0.0f);
    r.assign(n, 0.0f);
    uint32_t seed = 12345;
    for (size_t i = 0; i < n; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const float noise = ((float)(seed >> 8) / 16777216.0f) * 2.0f - 1.0f;
        const float t = (float)i / (float)kSampleRate;
        const float burst = (i >= 4800 && i < 9600) ? 0.5f * noise : 0.0f;
        l[i] = burst + 0.25f * std::sin(2.0f * 3.14159265f * 440.0f * t) * (t < 1.0f ? 1.0f : 0.0f);
        r[i] = -burst + 0.25f * std::sin(2.0f * 3.14159265f * 660.0f * t) * (t > 0.5f ? 1.0f : 0.0f);
    }
    l[0] = 1.0f;
    r[10] = 1.0f;
}

/* Every writable parameter away from its default, spread over its range. */
inline Changes spreadChanges(const ParamTable& table) {
    Changes c;
    for (int i = 0; i < table.count; ++i) {
        if (table[i].flags & kParamReadOnly) continue;
        c.push_back({ i, 0.15 + 0.7 * (double)((i * 37) % 23) / 22.0 });
    }
    return c;
}

/* A mid-stream automation move on a few parameters. */
inline Changes moveChanges(const ParamTable& table) {
    Changes c;
    for (int i = 0; i < table.count; i += 3) {
        if (table[i].flags & kParamReadOnly) continue;
        c.push_back({ i, 1.0 - (0.15 + 0.7 * (double)((i * 37) % 23) / 22.0) });
    }
    return c;
}

inline float maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return 1e9f;
    float m = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        const float d = std::fabs(a[i] - b[i]);
        if (!(d <= m)) m = std::isfinite(d) ? d : 1e9f;
    }
    return m;
}

inline bool allFinite(const std::vector<float>& a) {
    for (float v : a) if (!std::isfinite(v)) return false;
    return true;
}

/* The core run directly, the way the shell drives it: `initial` values
 * through prepare(), then each block every current value via
 * setParameterValueDirect() before process(). */
template <class Core>
void runCore(const std::vector<float>& inL, const std::vector<float>& inR, std::vector<float>& outL, std::vector<float>& outR,
             const std::vector<float>& initial, const Changes& first, const Changes& move) {
    const ParamTable& table = Core::table();
    Core core;
    std::vector<float> values = initial;
    for (int i = 0; i < table.count; ++i) core.setParameterValueDirect(i, values[(size_t)i]);
    core.prepare(kSampleRate, kBlock);
    outL = inL;
    outR = inR;
    for (int b = 0; b < kNumBlocks; ++b) {
        if (b == 0) for (auto& c : first) values[(size_t)c.first] = vst3::toPlain(table[c.first], c.second);
        if (b == kChangeBlock) for (auto& c : move) values[(size_t)c.first] = vst3::toPlain(table[c.first], c.second);
        for (int i = 0; i < table.count; ++i) core.setParameterValueDirect(i, values[(size_t)i]);
        core.process(outL.data() + (size_t)b * kBlock, outR.data() + (size_t)b * kBlock, kBlock);
    }
}

/* A minimal host around one processor/controller pair of the bundle. */
struct Plugin {
    IPtr<sv::IComponent> component;
    IPtr<sv::IAudioProcessor> processor;
    IPtr<sv::IEditController> controller;
    IPtr<sv::IConnectionPoint> componentCP, controllerCP;
    bool active = false;

    bool create(const VST3::Hosting::PluginFactory& factory, Steinberg::FUnknown* hostContext) {
        for (auto& info : factory.classInfos()) {
            if (info.category() != kVstAudioEffectClass) continue;
            component = factory.createInstance<sv::IComponent>(info.ID());
            break;
        }
        if (!component || component->initialize(hostContext) != kResultOk) return false;
        processor = Steinberg::U::cast<sv::IAudioProcessor>(component);
        Steinberg::TUID controllerCid;
        if (!processor || component->getControllerClassId(controllerCid) != kResultTrue) return false;
        controller = factory.createInstance<sv::IEditController>(VST3::UID(controllerCid));
        if (!controller || controller->initialize(hostContext) != kResultOk) return false;
        componentCP = Steinberg::U::cast<sv::IConnectionPoint>(component);
        controllerCP = Steinberg::U::cast<sv::IConnectionPoint>(controller);
        if (!componentCP || !controllerCP) return false;
        componentCP->connect(controllerCP);
        controllerCP->connect(componentCP);
        return true;
    }

    bool activate() {
        sv::SpeakerArrangement in = sv::SpeakerArr::kStereo, out = sv::SpeakerArr::kStereo;
        if (processor->setBusArrangements(&in, 1, &out, 1) != kResultTrue) return false;
        sv::ProcessSetup setup { sv::kRealtime, sv::kSample32, kBlock, kSampleRate };
        if (processor->setupProcessing(setup) != kResultOk) return false;
        if (component->activateBus(sv::kAudio, sv::kInput, 0, true) != kResultOk) return false;
        if (component->activateBus(sv::kAudio, sv::kOutput, 0, true) != kResultOk) return false;
        if (component->setActive(true) != kResultOk) return false;
        processor->setProcessing(true);
        active = true;
        return true;
    }

    template <class Core>
    bool processBlock(float* inL, float* inR, float* outL, float* outR, const Changes& changes) {
        sv::HostProcessData data;
        if (!data.prepare(*component, 0, sv::kSample32)) return false;
        float* ins[2] = { inL, inR };
        float* outs[2] = { outL, outR };
        data.setChannelBuffers(sv::kInput, 0, ins, 2);
        data.setChannelBuffers(sv::kOutput, 0, outs, 2);
        data.numSamples = kBlock;
        sv::ParameterChanges paramChanges((Steinberg::int32)changes.size());
        for (auto& c : changes) {
            Steinberg::int32 queueIndex = 0, pointIndex = 0;
            if (sv::IParamValueQueue* q = paramChanges.addParameterData(vst3::paramIdFor(Core::table()[c.first].id), queueIndex)) {
                q->addPoint(0, c.second, pointIndex);
            }
        }
        data.inputParameterChanges = &paramChanges;
        return processor->process(data) == kResultOk;
    }

    template <class Core>
    void run(const std::vector<float>& inL, const std::vector<float>& inR, std::vector<float>& outL, std::vector<float>& outR,
             const Changes& first, const Changes& move) {
        std::vector<float> l = inL, r = inR;
        outL.assign(inL.size(), 0.0f);
        outR.assign(inR.size(), 0.0f);
        for (int b = 0; b < kNumBlocks; ++b) {
            const Changes none;
            const Changes& changes = b == 0 ? first : (b == kChangeBlock ? move : none);
            const size_t o = (size_t)b * kBlock;
            LOUDIO_CHECK(processBlock<Core>(l.data() + o, r.data() + o, outL.data() + o, outR.data() + o, changes));
        }
    }

    std::vector<unsigned char> getState() {
        IPtr<Steinberg::MemoryStream> stream = Steinberg::owned(new Steinberg::MemoryStream());
        if (component->getState(stream) != kResultOk) return {};
        return std::vector<unsigned char>(stream->getData(), stream->getData() + stream->getSize());
    }

    bool setState(const std::vector<unsigned char>& blob) {
        IPtr<Steinberg::MemoryStream> a = Steinberg::owned(new Steinberg::MemoryStream((void*)blob.data(), (Steinberg::TSize)blob.size()));
        IPtr<Steinberg::MemoryStream> b = Steinberg::owned(new Steinberg::MemoryStream((void*)blob.data(), (Steinberg::TSize)blob.size()));
        return component->setState(a) == kResultOk && controller->setComponentState(b) == kResultOk;
    }

    void destroy() {
        if (active) {
            processor->setProcessing(false);
            component->setActive(false);
            active = false;
        }
        if (componentCP && controllerCP) {
            componentCP->disconnect(controllerCP);
            controllerCP->disconnect(componentCP);
        }
        if (controller) controller->terminate();
        if (component) component->terminate();
        componentCP = controllerCP = nullptr;
        controller = nullptr;
        processor = nullptr;
        component = nullptr;
    }
};

template <class Core>
void checkParameterMapping() {
    std::printf("parameter mapping\n");
    const ParamTable& table = Core::table();
    const vst3::ParamIndex index(table);
    LOUDIO_CHECK(table.indicesAreConsistent());
    LOUDIO_CHECK(index.unique());
    for (int i = 0; i < table.count; ++i) {
        const ParamMeta& m = table[i];
        LOUDIO_CHECK((index.ids[(size_t)i] & 0x80000000u) == 0);
        LOUDIO_CHECK(index.find(index.ids[(size_t)i]) == i);
        const float back = vst3::toPlain(m, vst3::toNormalized(m, m.defaultValue));
        LOUDIO_CHECK(std::fabs(back - m.defaultValue) <= 1e-5f * (m.maxValue - m.minValue));
        LOUDIO_CHECK(vst3::toPlain(m, 0.0) == m.minValue);
        LOUDIO_CHECK(vst3::toPlain(m, 1.0) == m.maxValue);
        if (m.flags & (kParamStepped | kParamBoolean)) {
            for (int s = 0; s <= vst3::stepCountFor(m); ++s) {
                const float plain = m.minValue + (float)s;
                LOUDIO_CHECK(vst3::toPlain(m, vst3::toNormalized(m, plain)) == plain);
            }
        }
    }
}

template <class Core>
void checkControllerParameters(Plugin& p) {
    std::printf("controller parameters\n");
    const ParamTable& table = Core::table();
    LOUDIO_CHECK(p.controller->getParameterCount() == table.count);
    for (int i = 0; i < table.count && i < p.controller->getParameterCount(); ++i) {
        sv::ParameterInfo info {};
        LOUDIO_CHECK(p.controller->getParameterInfo(i, info) == kResultOk);
        const ParamMeta& m = table[i];
        LOUDIO_CHECK(info.id == vst3::paramIdFor(m.id));
        LOUDIO_CHECK(Steinberg::Vst::StringConvert::convert(info.title) == m.name);
        LOUDIO_CHECK(info.stepCount == vst3::stepCountFor(m));
        LOUDIO_CHECK(std::fabs(info.defaultNormalizedValue - vst3::toNormalized(m, m.defaultValue)) < 1e-9);
        LOUDIO_CHECK(std::fabs(p.controller->getParamNormalized(info.id) - info.defaultNormalizedValue) < 1e-9);
        LOUDIO_CHECK(((info.flags & sv::ParameterInfo::kIsReadOnly) != 0) == ((m.flags & kParamReadOnly) != 0));
        /* Every value's text parses back to the same value. */
        for (double n : { 0.0, 0.5, 1.0 }) {
            sv::String128 text {};
            LOUDIO_CHECK(p.controller->getParamStringByValue(info.id, n, text) == kResultOk);
            sv::ParamValue back = -1.0;
            LOUDIO_CHECK(p.controller->getParamValueByString(info.id, text, back) == kResultOk);
            const float tolerance = (float)std::pow(10.0, -vst3::precisionFor(m)) * 0.51f;
            LOUDIO_CHECK(std::fabs(vst3::toPlain(m, back) - vst3::toPlain(m, n)) <= tolerance);
        }
    }
}

template <class Core>
void checkBuses(Plugin& p) {
    std::printf("buses\n");
    LOUDIO_CHECK(p.component->getBusCount(sv::kAudio, sv::kInput) == 1);
    LOUDIO_CHECK(p.component->getBusCount(sv::kAudio, sv::kOutput) == 1);
    LOUDIO_CHECK(p.component->getBusCount(sv::kEvent, sv::kInput) == 0);
    sv::SpeakerArrangement mono = sv::SpeakerArr::kMono, stereo = sv::SpeakerArr::kStereo, surround = sv::SpeakerArr::k51;
    LOUDIO_CHECK(p.processor->setBusArrangements(&mono, 1, &mono, 1) == kResultFalse);
    LOUDIO_CHECK(p.processor->setBusArrangements(&stereo, 1, &surround, 1) == kResultFalse);
    LOUDIO_CHECK(p.processor->setBusArrangements(&stereo, 1, &stereo, 1) == kResultTrue);
    sv::SpeakerArrangement arr = 0;
    LOUDIO_CHECK(p.processor->getBusArrangement(sv::kOutput, 0, arr) == kResultTrue && arr == sv::SpeakerArr::kStereo);
    LOUDIO_CHECK(p.processor->canProcessSampleSize(sv::kSample32) == kResultTrue);
    LOUDIO_CHECK(p.processor->canProcessSampleSize(sv::kSample64) == kResultFalse);
}

template <class Core>
std::vector<float> defaults() {
    std::vector<float> v((size_t)Core::table().count);
    Core::table().fillDefaults(v.data());
    return v;
}

/* Values after `first` then `move`, as plain values. */
template <class Core>
std::vector<float> valuesAfter(const Changes& first, const Changes& move) {
    std::vector<float> v = defaults<Core>();
    for (auto& c : first) v[(size_t)c.first] = vst3::toPlain(Core::table()[c.first], c.second);
    for (auto& c : move) v[(size_t)c.first] = vst3::toPlain(Core::table()[c.first], c.second);
    return v;
}

#ifdef _WIN32
template <class Core>
void checkSameSoundAsLpi(const char* dllPath, const std::vector<float>& inL, const std::vector<float>& inR,
                         const std::vector<float>& vstL, const std::vector<float>& vstR, const Changes& first, const Changes& move) {
    std::printf("same sound: VST3 vs LPI (%s)\n", dllPath);
    HMODULE m = LoadLibraryA(dllPath);
    LOUDIO_CHECK(m != nullptr);
    if (!m) return;
    auto getFactory = reinterpret_cast<lpi_get_factory_fn>(reinterpret_cast<void*>(GetProcAddress(m, LPI_GET_FACTORY_SYMBOL_NAME)));
    LOUDIO_CHECK(getFactory != nullptr);
    if (!getFactory) return;
    const lpi_plugin_api* api = getFactory()->get_api();
    lpi_plugin* plugin = api->create(kSampleRate, kBlock);
    LOUDIO_CHECK(plugin && api->activate(plugin));
    if (!plugin) return;
    const ParamTable& table = Core::table();
    std::vector<float> outL(inL.size()), outR(inR.size());
    for (int b = 0; b < kNumBlocks; ++b) {
        if (b == 0) for (auto& c : first) LOUDIO_CHECK(api->set_parameter_value(plugin, (uint32_t)c.first, vst3::toPlain(table[c.first], c.second)));
        if (b == kChangeBlock) for (auto& c : move) LOUDIO_CHECK(api->set_parameter_value(plugin, (uint32_t)c.first, vst3::toPlain(table[c.first], c.second)));
        const size_t o = (size_t)b * kBlock;
        const float* ins[2] = { inL.data() + o, inR.data() + o };
        float* outs[2] = { outL.data() + o, outR.data() + o };
        lpi_process_data d = { ins, outs, 2, 2, (uint32_t)kBlock };
        api->process(plugin, &d);
    }
    api->deactivate(plugin);
    api->destroy(plugin);
    const float dl = maxAbsDiff(vstL, outL), dr = maxAbsDiff(vstR, outR);
    std::printf("  max |diff| L=%g R=%g\n", dl, dr);
    LOUDIO_CHECK(dl <= kTolerance && dr <= kTolerance);
}
#endif

/* Runs every generic check against the bundle; returns the process exit
 * code. `extra`, if given, runs plugin-specific checks on an activated
 * instance that has processed the test signal. */
template <class Core>
int runVst3CoreTests(const char* bundlePath, const char* name, const char* subCategories, const char* version,
                     int argc, char** argv, void (*extra)(Plugin&) = nullptr) {
    checkParameterMapping<Core>();

    std::string error;
    auto module = VST3::Hosting::Module::create(bundlePath, error);
    if (!module) {
        std::printf("FAIL: cannot load %s: %s\n", bundlePath, error.c_str());
        return 1;
    }
    const VST3::Hosting::PluginFactory& factory = module->getFactory();
    std::printf("factory: %s, %zu classes\n", factory.info().vendor().c_str(), factory.classInfos().size());
    LOUDIO_CHECK(factory.classInfos().size() == 2);
    for (auto& info : factory.classInfos()) {
        LOUDIO_CHECK(info.name() == name);
        LOUDIO_CHECK(info.version() == version);
        if (info.category() == kVstAudioEffectClass) LOUDIO_CHECK(info.subCategoriesString() == subCategories);
    }

    IPtr<sv::HostApplication> host = Steinberg::owned(new sv::HostApplication());
    std::vector<float> inL, inR, vstL, vstR;
    makeInput(inL, inR);
    const Changes first = spreadChanges(Core::table());
    const Changes move = moveChanges(Core::table());
    const std::vector<float> expected = valuesAfter<Core>(first, move);

    Plugin p;
    LOUDIO_CHECK(p.create(factory, host));
    if (p.component) {
        checkControllerParameters<Core>(p);
        checkBuses<Core>(p);

        std::printf("latency + same sound: VST3 vs core\n");
        LOUDIO_CHECK(p.activate());
        {
            Core core;
            core.prepare(kSampleRate, kBlock);
            LOUDIO_CHECK(p.processor->getLatencySamples() == vst3::coreLatency(core));
        }
        p.run<Core>(inL, inR, vstL, vstR, first, move);
        std::vector<float> refL, refR;
        runCore<Core>(inL, inR, refL, refR, defaults<Core>(), first, move);
        const float dl = maxAbsDiff(vstL, refL), dr = maxAbsDiff(vstR, refR);
        std::printf("  max |diff| L=%g R=%g\n", dl, dr);
        LOUDIO_CHECK(allFinite(vstL) && allFinite(vstR));
        LOUDIO_CHECK(dl <= kTolerance && dr <= kTolerance);

        std::printf("state save/restore\n");
        const std::vector<unsigned char> blob = p.getState();
        Core core;
        for (int i = 0; i < Core::table().count; ++i) core.setParameterValueDirect(i, expected[(size_t)i]);
        std::vector<unsigned char> coreBlob(core.getState(nullptr, 0));
        core.getState(coreBlob.data(), coreBlob.size());
        LOUDIO_CHECK(!blob.empty() && blob == coreBlob);

        Plugin q;
        LOUDIO_CHECK(q.create(factory, host));
        if (q.component) {
            LOUDIO_CHECK(q.setState(blob));
            for (int i = 0; i < Core::table().count; ++i) {
                const ParamMeta& m = Core::table()[i];
                const sv::ParamValue n = q.controller->getParamNormalized(vst3::paramIdFor(m.id));
                LOUDIO_CHECK(std::fabs(vst3::toPlain(m, n) - expected[(size_t)i]) <= 1e-5f * (m.maxValue - m.minValue));
            }
            LOUDIO_CHECK(q.getState() == blob);
            LOUDIO_CHECK(q.activate());
            std::vector<float> outL, outR, stateRefL, stateRefR;
            q.run<Core>(inL, inR, outL, outR, {}, {});
            runCore<Core>(inL, inR, stateRefL, stateRefR, expected, {}, {});
            LOUDIO_CHECK(maxAbsDiff(outL, stateRefL) <= kTolerance && maxAbsDiff(outR, stateRefR) <= kTolerance);

            std::vector<unsigned char> bad = blob;
            bad[0] ^= 0x5A;
            IPtr<Steinberg::MemoryStream> s = Steinberg::owned(new Steinberg::MemoryStream(bad.data(), (Steinberg::TSize)bad.size()));
            LOUDIO_CHECK(q.component->setState(s) != kResultOk);
            LOUDIO_CHECK(q.getState() == blob);
            q.destroy();
        }

        if (extra) extra(p);
    }
    p.destroy();

#ifdef _WIN32
    if (argc > 1) checkSameSoundAsLpi<Core>(argv[1], inL, inR, vstL, vstR, first, move);
#else
    (void)argc;
    (void)argv;
#endif

    if (failures()) {
        std::printf("%d check(s) FAILED\n", failures());
        return 1;
    }
    std::printf("all VST3 tests passed (%s)\n", name);
    return 0;
}

} // namespace vst3test
} // namespace loudio
