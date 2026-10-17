/*
 * The EQ8's VST3 build (#171), tested against the built bundle with the
 * shared kit (VSTs/common/vst3/Vst3TestHost.h): factory, parameters,
 * buses, latency, state, and the same sound as EQ8Core run directly
 * and, given lpi_eq8.dll (Windows CI), as the LPI build.
 */
#include "vst3/Vst3TestHost.h"
#include "EQ8Core.h"

int main(int argc, char** argv) {
    return loudio::vst3test::runVst3CoreTests<EQ8Core>(
        LOUDIO_VST3_BUNDLE, "Loudio EQ-8", "Fx|EQ", LOUDIO_PLUGIN_VERSION, argc, argv);
}
