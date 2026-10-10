# Loudio Scratch

Scaffolded by `tools/new_plugin.py Scratch` on the plugin platform (#125).

| Piece | File |
|---|---|
| Parameter table | `src/ScratchParams.h` |
| DSP core (P1 contract) | `src/ScratchCore.{h,cpp}` |
| cuif UI (shared by both editors) | `src/ScratchUi.{h,cpp}` |
| LPI build (P2 template) | `src/lpi_scratch.cpp` |
| VST3/Standalone build (P3 templates) | `src/PluginProcessor.{h,cpp}` |
| Tests | `src/DSPTests.cpp` (generic kit + own), `test/lpi_scratch_gui_harness.c`, `test/golden/` |

Next steps: write the DSP in `ScratchCore::process()`, grow the table,
lay out the UI, add plugin-specific tests, then `dsp_tests --regen-golden`
and commit the new goldens. CI already builds and tests this plugin
(tools/list_plugins.py feeds the matrix).

Build (Windows, from the repo root):

    cmake -S VSTs/Scratch -B build/scratch
    cmake --build build/scratch --config Debug --target dsp_tests lpi_scratch lpi_scratch_gui_harness lpi_conformance
    cmake --build build/scratch --config Debug --target LoudioScratch_VST3 LoudioScratch_Standalone

DSP tests only (any platform):

    cmake -S VSTs/Scratch -B build/scratch-tests -DLOUDIO_PLUGIN_TESTS_ONLY=ON
    cmake --build build/scratch-tests --target dsp_tests && ./build/scratch-tests/dsp_tests
