/*
 * The Gate's VST3 build (#171), tested against the built bundle with the
 * shared kit (VSTs/common/vst3/Vst3TestHost.h): factory, parameters,
 * buses, latency, state, and the same sound as GateCore run directly
 * and, given lpi_gate.dll (Windows CI), as the LPI build.
 */
#include "vst3/Vst3TestHost.h"
#include "GateCore.h"

int main(int argc, char** argv) {
    return loudio::vst3test::runVst3CoreTests<GateCore>(
        LOUDIO_VST3_BUNDLE, "Loudio Gate", "Fx|Dynamics", LOUDIO_PLUGIN_VERSION, argc, argv);
}
