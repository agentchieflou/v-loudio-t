/*
 * The EQ3's VST3 build (#171), tested against the built bundle with the
 * shared kit (VSTs/common/vst3/Vst3TestHost.h): factory, parameters,
 * buses, latency, state, and the same sound as EQ3Core run directly
 * and, given lpi_eq3.dll (Windows CI), as the LPI build.
 */
#include "vst3/Vst3TestHost.h"
#include "EQ3Core.h"

int main(int argc, char** argv) {
    return loudio::vst3test::runVst3CoreTests<EQ3Core>(
        LOUDIO_VST3_BUNDLE, "Loudio EQ-3", "Fx|EQ", LOUDIO_PLUGIN_VERSION, argc, argv);
}
