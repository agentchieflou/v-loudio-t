/*
 * The Glue's VST3 build (#171), tested against the built bundle with the
 * shared kit (VSTs/common/vst3/Vst3TestHost.h): factory, parameters,
 * buses, latency, state, and the same sound as GlueCore run directly
 * and, given lpi_glue.dll (Windows CI), as the LPI build.
 */
#include "vst3/Vst3TestHost.h"
#include "GlueCore.h"

int main(int argc, char** argv) {
    return loudio::vst3test::runVst3CoreTests<GlueCore>(
        LOUDIO_VST3_BUNDLE, "Loudio Glue", "Fx|Dynamics", LOUDIO_PLUGIN_VERSION, argc, argv);
}
