/*
 * Tests for the Reverb's VST3 build (#171), against the built bundle:
 * loaded the way a host loads it (VST3::Hosting::Module, the SDK's hosting
 * classes), never linked in.
 *
 *   vst3_reverb_tests                 parameter mapping, factory/classes,
 *                                     buses, state, and same sound against
 *                                     ReverbCore run directly
 *   vst3_reverb_tests <lpi_reverb.dll> also same sound against the LPI
 *                                     Reverb (Windows CI passes it)
 *
 * "Same sound": identical input, parameters set at the same block
 * boundaries (block 0, and an automation change at block kChangeBlock),
 * outputs equal within kTolerance.
 */
#include "vst3/LoudioVst3.h"
#include "ReverbCore.h"

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

using namespace Steinberg;
using namespace Steinberg::Vst;
namespace lv = loudio::vst3;

namespace {

int g_failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 256;
constexpr int kNumBlocks = 375; /* 2 s */
constexpr int kChangeBlock = 150;
constexpr float kTolerance = 1e-6f;

const loudio::ParamTable& table() { return ReverbCore::table(); }

/* A stereo test signal with an impulse, a noise burst and a tone. */
void makeInput(std::vector<float>& l, std::vector<float>& r) {
    const size_t n = (size_t)kBlock * kNumBlocks;
    l.assign(n, 0.0f);
    r.assign(n, 0.0f);
    uint32_t seed = 12345;
    for (size_t i = 0; i < n; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const float noise = ((float)(seed >> 8) / 16777216.0f) * 2.0f - 1.0f;
        const float t = (float)i / (float)kSampleRate;
        float burst = (i >= 4800 && i < 9600) ? 0.5f * noise : 0.0f;
        l[i] = burst + 0.25f * std::sin(2.0f * 3.14159265f * 440.0f * t) * (t < 1.0f ? 1.0f : 0.0f);
        r[i] = -burst + 0.25f * std::sin(2.0f * 3.14159265f * 660.0f * t) * (t > 0.5f ? 1.0f : 0.0f);
    }
    l[0] = 1.0f;
    r[10] = 1.0f;
}

/* Normalized values for every parameter, block 0: away from defaults, a
 * non-default mode, freeze off. */
std::vector<std::pair<int, double>> firstChanges() {
    std::vector<std::pair<int, double>> c;
    for (int i = 0; i < table().count; ++i) {
        double v = 0.15 + 0.7 * (double)((i * 37) % 23) / 22.0;
        if (i == kParamFreeze) v = 0.0;
        if (i == kParamMode) v = 0.5; /* Plate */
        if (i == kParamDuckThreshold || i == kParamGateThreshold) v = 0.2;
        c.push_back({ i, v });
    }
    return c;
}

/* An automation move mid-stream. */
std::vector<std::pair<int, double>> secondChanges() {
    return { { kParamDecayTime, 0.8 }, { kParamDryWet, 0.9 }, { kParamMode, 0.75 }, { kParamDamping, 0.05 } };
}

float maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    float m = 0.0f;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) m = std::fmax(m, std::fabs(a[i] - b[i]));
    return a.size() == b.size() ? m : 1e9f;
}

bool allFinite(const std::vector<float>& a) {
    for (float v : a) if (!std::isfinite(v)) return false;
    return true;
}

/* ReverbCore run directly, the way the shell drives it: defaults through
 * prepare(), then each block every current value via
 * setParameterValueDirect() before process(). */
void runReference(const std::vector<float>& inL, const std::vector<float>& inR, std::vector<float>& outL, std::vector<float>& outR,
                  const std::vector<float>* initialValues = nullptr) {
    ReverbCore core;
    std::vector<float> values((size_t)table().count);
    table().fillDefaults(values.data());
    if (initialValues) values = *initialValues;
    for (int i = 0; i < table().count; ++i) core.setParameterValueDirect(i, values[(size_t)i]);
    core.prepare(kSampleRate, kBlock);
    outL = inL;
    outR = inR;
    for (int b = 0; b < kNumBlocks; ++b) {
        if (!initialValues) {
            if (b == 0) for (auto& c : firstChanges()) values[(size_t)c.first] = lv::toPlain(table()[c.first], c.second);
            if (b == kChangeBlock) for (auto& c : secondChanges()) values[(size_t)c.first] = lv::toPlain(table()[c.first], c.second);
        }
        for (int i = 0; i < table().count; ++i) core.setParameterValueDirect(i, values[(size_t)i]);
        core.process(outL.data() + (size_t)b * kBlock, outR.data() + (size_t)b * kBlock, kBlock);
    }
}

/* ---- A minimal host around the bundle ---- */

struct Plugin {
    IPtr<IComponent> component;
    IPtr<IAudioProcessor> processor;
    IPtr<IEditController> controller;
    IPtr<IConnectionPoint> componentCP, controllerCP;
    bool active = false;

    bool create(const VST3::Hosting::PluginFactory& factory, FUnknown* hostContext) {
        for (auto& info : factory.classInfos()) {
            if (info.category() != kVstAudioEffectClass) continue;
            component = factory.createInstance<IComponent>(info.ID());
            break;
        }
        if (!component || component->initialize(hostContext) != kResultOk) return false;
        processor = U::cast<IAudioProcessor>(component);
        TUID controllerCid;
        if (!processor || component->getControllerClassId(controllerCid) != kResultTrue) return false;
        controller = factory.createInstance<IEditController>(VST3::UID(controllerCid));
        if (!controller || controller->initialize(hostContext) != kResultOk) return false;
        componentCP = U::cast<IConnectionPoint>(component);
        controllerCP = U::cast<IConnectionPoint>(controller);
        if (!componentCP || !controllerCP) return false;
        componentCP->connect(controllerCP);
        controllerCP->connect(componentCP);
        return true;
    }

    bool activate() {
        SpeakerArrangement in = SpeakerArr::kStereo, out = SpeakerArr::kStereo;
        if (processor->setBusArrangements(&in, 1, &out, 1) != kResultTrue) return false;
        ProcessSetup setup { kRealtime, kSample32, kBlock, kSampleRate };
        if (processor->setupProcessing(setup) != kResultOk) return false;
        if (component->activateBus(kAudio, kInput, 0, true) != kResultOk) return false;
        if (component->activateBus(kAudio, kOutput, 0, true) != kResultOk) return false;
        if (component->setActive(true) != kResultOk) return false;
        processor->setProcessing(true);
        active = true;
        return true;
    }

    bool processBlock(float* inL, float* inR, float* outL, float* outR, const std::vector<std::pair<int, double>>& changes) {
        HostProcessData data;
        if (!data.prepare(*component, 0, kSample32)) return false;
        float* ins[2] = { inL, inR };
        float* outs[2] = { outL, outR };
        data.setChannelBuffers(kInput, 0, ins, 2);
        data.setChannelBuffers(kOutput, 0, outs, 2);
        data.numSamples = kBlock;
        ParameterChanges paramChanges((int32)changes.size());
        for (auto& c : changes) {
            int32 queueIndex = 0, pointIndex = 0;
            if (IParamValueQueue* q = paramChanges.addParameterData(lv::paramIdFor(table()[c.first].id), queueIndex)) {
                q->addPoint(0, c.second, pointIndex);
            }
        }
        data.inputParameterChanges = &paramChanges;
        return processor->process(data) == kResultOk;
    }

    void run(const std::vector<float>& inL, const std::vector<float>& inR, std::vector<float>& outL, std::vector<float>& outR, bool automate) {
        std::vector<float> l = inL, r = inR;
        outL.assign(inL.size(), 0.0f);
        outR.assign(inR.size(), 0.0f);
        for (int b = 0; b < kNumBlocks; ++b) {
            std::vector<std::pair<int, double>> changes;
            if (automate && b == 0) changes = firstChanges();
            if (automate && b == kChangeBlock) changes = secondChanges();
            const size_t o = (size_t)b * kBlock;
            CHECK(processBlock(l.data() + o, r.data() + o, outL.data() + o, outR.data() + o, changes));
        }
    }

    std::vector<unsigned char> getState() {
        IPtr<MemoryStream> stream = owned(new MemoryStream());
        if (component->getState(stream) != kResultOk) return {};
        return std::vector<unsigned char>(stream->getData(), stream->getData() + stream->getSize());
    }

    bool setState(const std::vector<unsigned char>& blob) {
        IPtr<MemoryStream> a = owned(new MemoryStream((void*)blob.data(), (TSize)blob.size()));
        IPtr<MemoryStream> b = owned(new MemoryStream((void*)blob.data(), (TSize)blob.size()));
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

/* ---- Tests ---- */

void testParameterMapping() {
    std::printf("parameter mapping\n");
    CHECK(lv::ParamIndex(table()).unique());
    for (int i = 0; i < table().count; ++i) {
        const loudio::ParamMeta& m = table()[i];
        const lv::ParamIndex index(table());
        CHECK((index.ids[(size_t)i] & 0x80000000u) == 0);
        CHECK(index.find(index.ids[(size_t)i]) == i);
        const float back = lv::toPlain(m, lv::toNormalized(m, m.defaultValue));
        CHECK(std::fabs(back - m.defaultValue) <= 1e-5f * (m.maxValue - m.minValue));
        CHECK(lv::toPlain(m, 0.0) == m.minValue);
        CHECK(lv::toPlain(m, 1.0) == m.maxValue);
        if (m.flags & (loudio::kParamStepped | loudio::kParamBoolean)) {
            for (int s = 0; s <= lv::stepCountFor(m); ++s) {
                const float plain = m.minValue + (float)s;
                CHECK(lv::toPlain(m, lv::toNormalized(m, plain)) == plain);
            }
        }
    }
    CHECK(lv::stepCountFor(table()[kParamMode]) == 4);
    CHECK(lv::stepCountFor(table()[kParamFreeze]) == 1);
    CHECK(lv::stepCountFor(table()[kParamWidth]) == 0);
}

void testClassesAndParameters(Plugin& p) {
    std::printf("controller parameters\n");
    CHECK(p.controller->getParameterCount() == table().count);
    for (int i = 0; i < table().count && i < p.controller->getParameterCount(); ++i) {
        ParameterInfo info {};
        CHECK(p.controller->getParameterInfo(i, info) == kResultOk);
        const loudio::ParamMeta& m = table()[i];
        CHECK(info.id == lv::paramIdFor(m.id));
        CHECK(info.stepCount == lv::stepCountFor(m));
        CHECK(std::fabs(info.defaultNormalizedValue - lv::toNormalized(m, m.defaultValue)) < 1e-9);
        CHECK(std::fabs(p.controller->getParamNormalized(info.id) - info.defaultNormalizedValue) < 1e-9);
        CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
    }
    /* Labels: the mode list and its round trip. */
    const ParamID mode = lv::paramIdFor(table()[kParamMode].id);
    String128 text {};
    CHECK(p.controller->getParamStringByValue(mode, 0.5, text) == kResultOk);
    CHECK(Steinberg::Vst::StringConvert::convert(text) == "Plate");
    ParamValue normalized = -1.0;
    CHECK(p.controller->getParamValueByString(mode, text, normalized) == kResultOk);
    CHECK(normalized == 0.5);
    const ParamID decay = lv::paramIdFor(table()[kParamDecayTime].id);
    CHECK(p.controller->getParamStringByValue(decay, 0.0, text) == kResultOk);
    CHECK(Steinberg::Vst::StringConvert::convert(text) == "0.4");
}

void testBuses(Plugin& p) {
    std::printf("buses and latency\n");
    CHECK(p.component->getBusCount(kAudio, kInput) == 1);
    CHECK(p.component->getBusCount(kAudio, kOutput) == 1);
    CHECK(p.component->getBusCount(kEvent, kInput) == 0);
    SpeakerArrangement mono = SpeakerArr::kMono, stereo = SpeakerArr::kStereo, surround = SpeakerArr::k51;
    CHECK(p.processor->setBusArrangements(&mono, 1, &mono, 1) == kResultFalse);
    CHECK(p.processor->setBusArrangements(&stereo, 1, &surround, 1) == kResultFalse);
    CHECK(p.processor->setBusArrangements(&stereo, 1, &stereo, 1) == kResultTrue);
    SpeakerArrangement arr = 0;
    CHECK(p.processor->getBusArrangement(kOutput, 0, arr) == kResultTrue && arr == SpeakerArr::kStereo);
    CHECK(p.processor->canProcessSampleSize(kSample32) == kResultTrue);
    CHECK(p.processor->canProcessSampleSize(kSample64) == kResultFalse);
    CHECK(p.processor->getLatencySamples() == 0);
}

void testSameSoundAsCore(Plugin& p, const std::vector<float>& inL, const std::vector<float>& inR,
                         std::vector<float>& vstL, std::vector<float>& vstR) {
    std::printf("same sound: VST3 vs ReverbCore\n");
    CHECK(p.activate());
    p.run(inL, inR, vstL, vstR, true);
    std::vector<float> refL, refR;
    runReference(inL, inR, refL, refR);
    const float dl = maxAbsDiff(vstL, refL), dr = maxAbsDiff(vstR, refR);
    std::printf("  max |diff| L=%g R=%g\n", dl, dr);
    CHECK(allFinite(vstL) && allFinite(vstR));
    CHECK(dl <= kTolerance && dr <= kTolerance);
    CHECK(maxAbsDiff(vstL, inL) > 0.01f); /* it really did something */

    /* Tail: 0.15 s pre-delay + 15 s x 2.0 band decay at most; freeze holds it. */
    CHECK(p.processor->getTailSamples() == (uint32)std::ceil(30.15 * kSampleRate));
    std::vector<float> l(kBlock, 0.0f), r(kBlock, 0.0f), ol(kBlock), orr(kBlock);
    CHECK(p.processBlock(l.data(), r.data(), ol.data(), orr.data(), { { kParamFreeze, 1.0 } }));
    CHECK(p.processor->getTailSamples() == kInfiniteTail);
    CHECK(p.processBlock(l.data(), r.data(), ol.data(), orr.data(), { { kParamFreeze, 0.0 } }));
    CHECK(p.processor->getTailSamples() != kInfiniteTail);
}

void testState(Plugin& p, const VST3::Hosting::PluginFactory& factory, FUnknown* host,
               const std::vector<float>& inL, const std::vector<float>& inR) {
    std::printf("state save/restore\n");
    const std::vector<unsigned char> blob = p.getState();
    CHECK(blob.size() > 8 && std::memcmp(blob.data(), "LRVB", 4) == 0);

    /* The blob is exactly ReverbCore's, for the values the plugin holds. */
    std::vector<float> expected((size_t)table().count);
    table().fillDefaults(expected.data());
    for (auto& c : firstChanges()) expected[(size_t)c.first] = lv::toPlain(table()[c.first], c.second);
    for (auto& c : secondChanges()) expected[(size_t)c.first] = lv::toPlain(table()[c.first], c.second);
    ReverbCore core;
    for (int i = 0; i < table().count; ++i) core.setParameterValueDirect(i, expected[(size_t)i]);
    std::vector<unsigned char> coreBlob(core.getState(nullptr, 0));
    core.getState(coreBlob.data(), coreBlob.size());
    CHECK(blob == coreBlob);

    /* A fresh instance restores it: controller values, saved bytes and sound. */
    Plugin q;
    CHECK(q.create(factory, host));
    CHECK(q.setState(blob));
    for (int i = 0; i < table().count; ++i) {
        const ParamValue n = q.controller->getParamNormalized(lv::paramIdFor(table()[i].id));
        CHECK(std::fabs(lv::toPlain(table()[i], n) - expected[(size_t)i]) <= 1e-5f * (table()[i].maxValue - table()[i].minValue));
    }
    CHECK(q.getState() == blob);
    CHECK(q.activate());
    std::vector<float> outL, outR, refL, refR;
    q.run(inL, inR, outL, outR, false);
    runReference(inL, inR, refL, refR, &expected);
    CHECK(maxAbsDiff(outL, refL) <= kTolerance && maxAbsDiff(outR, refR) <= kTolerance);

    /* A corrupt blob is rejected and changes nothing. */
    std::vector<unsigned char> bad = blob;
    bad[0] = 'X';
    IPtr<MemoryStream> s = owned(new MemoryStream(bad.data(), (TSize)bad.size()));
    CHECK(q.component->setState(s) != kResultOk);
    CHECK(q.getState() == blob);
    q.destroy();
}

#ifdef _WIN32
void testSameSoundAsLpi(const char* dllPath, const std::vector<float>& inL, const std::vector<float>& inR,
                        const std::vector<float>& vstL, const std::vector<float>& vstR) {
    std::printf("same sound: VST3 vs LPI (%s)\n", dllPath);
    HMODULE m = LoadLibraryA(dllPath);
    CHECK(m != nullptr);
    if (!m) return;
    auto getFactory = (lpi_get_factory_fn)GetProcAddress(m, LPI_GET_FACTORY_SYMBOL_NAME);
    CHECK(getFactory != nullptr);
    if (!getFactory) return;
    const lpi_plugin_api* api = getFactory()->get_api();
    lpi_plugin* plugin = api->create(kSampleRate, kBlock);
    CHECK(plugin && api->activate(plugin));
    std::vector<float> outL(inL.size()), outR(inR.size());
    for (int b = 0; b < kNumBlocks; ++b) {
        if (b == 0) for (auto& c : firstChanges()) CHECK(api->set_parameter_value(plugin, (uint32_t)c.first, lv::toPlain(table()[c.first], c.second)));
        if (b == kChangeBlock) for (auto& c : secondChanges()) CHECK(api->set_parameter_value(plugin, (uint32_t)c.first, lv::toPlain(table()[c.first], c.second)));
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
    CHECK(dl <= kTolerance && dr <= kTolerance);
}
#endif

} // namespace

int main(int argc, char** argv) {
    testParameterMapping();

    std::string error;
    auto module = VST3::Hosting::Module::create(LOUDIO_REVERB_VST3_BUNDLE, error);
    if (!module) {
        std::printf("FAIL: cannot load %s: %s\n", LOUDIO_REVERB_VST3_BUNDLE, error.c_str());
        return 1;
    }
    const VST3::Hosting::PluginFactory& factory = module->getFactory();
    std::printf("factory: %s, %zu classes\n", factory.info().vendor().c_str(), factory.classInfos().size());
    CHECK(factory.classInfos().size() == 2);
    for (auto& info : factory.classInfos()) {
        CHECK(info.name() == "Loudio Reverb");
        CHECK(info.version() == LOUDIO_PLUGIN_VERSION);
        if (info.category() == kVstAudioEffectClass) CHECK(info.subCategoriesString() == "Fx|Reverb");
    }

    IPtr<HostApplication> host = owned(new HostApplication());

    std::vector<float> inL, inR, vstL, vstR;
    makeInput(inL, inR);

    Plugin p;
    CHECK(p.create(factory, host));
    if (p.component) {
        testClassesAndParameters(p);
        testBuses(p);
        testSameSoundAsCore(p, inL, inR, vstL, vstR);
        testState(p, factory, host, inL, inR);
    }
    p.destroy();

#ifdef _WIN32
    if (argc > 1) testSameSoundAsLpi(argv[1], inL, inR, vstL, vstR);
#else
    (void)argc;
    (void)argv;
#endif

    if (g_failures) {
        std::printf("%d check(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("all vst3_reverb_tests passed\n");
    return 0;
}
