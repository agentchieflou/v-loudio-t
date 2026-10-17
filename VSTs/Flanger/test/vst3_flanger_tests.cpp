/*
 * The Flanger's VST3 build (#171), tested against the built bundle with the
 * shared kit (VSTs/common/vst3/Vst3TestHost.h): factory, parameters,
 * buses, latency, state, and the same sound as FlangerCore run directly
 * and, given lpi_flanger.dll (Windows CI), as the LPI build.
 */
#include "vst3/Vst3TestHost.h"
#include "FlangerCore.h"

int main(int argc, char** argv) {
    return loudio::vst3test::runVst3CoreTests<FlangerCore>(
        LOUDIO_VST3_BUNDLE, "Loudio Flanger", "Fx|Modulation", LOUDIO_PLUGIN_VERSION, argc, argv);
}
