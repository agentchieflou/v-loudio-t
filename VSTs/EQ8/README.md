# Loudio EQ-8

8-band parametric EQ (#136), scaffolded by `tools/new_plugin.py EQ8` on the plugin platform (#125) and built on `loudio::BiquadD` (double-precision TDF2, RBJ designs). Blueprint: `Plugins/EQ/EQ-8/README.md`.

| Piece | File |
|---|---|
| Parameter table | `src/EQ8Params.h` |
| DSP core (P1 contract) | `src/EQ8Core.{h,cpp}` |
| cuif UI (shared by both editors) | `src/EQ8Ui.{h,cpp}` |
| LPI build (P2 template) | `src/lpi_eq8.cpp` |
| VST3/Standalone build (P3 templates) | `src/PluginProcessor.{h,cpp}` |
| Tests | `src/DSPTests.cpp` (generic kit + own), `test/lpi_eq8_gui_harness.c`, `test/golden/` |

Parameters: 8 bands x {type (Off/LP/HP/low shelf/high shelf/bell/notch),
frequency, gain, Q} + output gain + bypass. The UI is the EQ curve widget
over a post-EQ analyzer (drag a handle for frequency/gain, wheel for Q),
one type dropdown per band, an output gain knob and a bypass button; the
curve is computed from the UI's parameter copy (`EQ8Response.h`).

After an intentional DSP change: `dsp_tests --regen-golden` and commit
`test/golden/`. CI builds and tests this plugin automatically.

Build (Windows, from the repo root):

    cmake -S VSTs/EQ8 -B build/eq8
    cmake --build build/eq8 --config Debug --target dsp_tests lpi_eq8 lpi_eq8_gui_harness lpi_conformance
    cmake --build build/eq8 --config Debug --target LoudioEQ8_VST3 LoudioEQ8_Standalone

DSP tests only (any platform):

    cmake -S VSTs/EQ8 -B build/eq8-tests -DLOUDIO_PLUGIN_TESTS_ONLY=ON
    cmake --build build/eq8-tests --target dsp_tests && ./build/eq8-tests/dsp_tests
