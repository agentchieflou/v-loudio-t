/*
 * The AutoTune's VST3 build (#171), tested against the built bundle with the
 * shared kit (VSTs/common/vst3/Vst3TestHost.h): factory, parameters,
 * buses, latency, state, and the same sound as AutoTuneCore run directly
 * and, given lpi_autotune.dll (Windows CI), as the LPI build.
 */
#include "vst3/Vst3TestHost.h"
#include "AutoTuneCore.h"

int main(int argc, char** argv) {
    return loudio::vst3test::runVst3CoreTests<AutoTuneCore>(
        LOUDIO_VST3_BUNDLE, "Loudio AutoTune", "Fx|Pitch Shift", LOUDIO_PLUGIN_VERSION, argc, argv);
}
