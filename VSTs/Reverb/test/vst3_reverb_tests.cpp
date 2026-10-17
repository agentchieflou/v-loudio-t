/*
 * The Reverb's VST3 build (#171), tested against the built bundle with the
 * shared kit (VSTs/common/vst3/Vst3TestHost.h): factory, parameters,
 * buses, latency, state, and the same sound as ReverbCore run directly
 * and, given lpi_reverb.dll (Windows CI), as the LPI Reverb. On top of
 * that, the Reverb's own: mode labels, display text, and the tail.
 */
#include "vst3/Vst3TestHost.h"
#include "ReverbCore.h"

#include <cmath>

namespace {

namespace lv = loudio::vst3;
namespace lt = loudio::vst3test;
using namespace Steinberg;
using namespace Steinberg::Vst;

void reverbChecks(lt::Plugin& p) {
    std::printf("reverb: labels, text, tail\n");
    const ParamID mode = lv::paramIdFor(ReverbCore::table()[kParamMode].id);
    String128 text {};
    LOUDIO_CHECK(p.controller->getParamStringByValue(mode, 0.5, text) == kResultOk);
    LOUDIO_CHECK(Steinberg::Vst::StringConvert::convert(text) == "Plate");
    ParamValue normalized = -1.0;
    LOUDIO_CHECK(p.controller->getParamValueByString(mode, text, normalized) == kResultOk);
    LOUDIO_CHECK(normalized == 0.5);
    const ParamID decay = lv::paramIdFor(ReverbCore::table()[kParamDecayTime].id);
    LOUDIO_CHECK(p.controller->getParamStringByValue(decay, 0.0, text) == kResultOk);
    LOUDIO_CHECK(Steinberg::Vst::StringConvert::convert(text) == "0.4");

    /* Tail: 0.15 s pre-delay + 15 s x 2.0 band decay at most; freeze holds it. */
    std::vector<float> l(lt::kBlock, 0.0f), r(lt::kBlock, 0.0f), ol(lt::kBlock), orr(lt::kBlock);
    LOUDIO_CHECK(p.processBlock<ReverbCore>(l.data(), r.data(), ol.data(), orr.data(), { { kParamFreeze, 0.0 } }));
    LOUDIO_CHECK(p.processor->getTailSamples() == (uint32)std::ceil(30.15 * lt::kSampleRate));
    LOUDIO_CHECK(p.processBlock<ReverbCore>(l.data(), r.data(), ol.data(), orr.data(), { { kParamFreeze, 1.0 } }));
    LOUDIO_CHECK(p.processor->getTailSamples() == kInfiniteTail);
}

} // namespace

int main(int argc, char** argv) {
    return lt::runVst3CoreTests<ReverbCore>(LOUDIO_VST3_BUNDLE, "Loudio Reverb", "Fx|Reverb", LOUDIO_PLUGIN_VERSION, argc, argv, &reverbChecks);
}
