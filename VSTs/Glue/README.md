# Loudio Glue

VCA bus compressor, feedback topology (#140), on the plugin platform
(#125). Blueprint: `Plugins/Compressors/Glue/README.md` (with the
platform amendment at its top). Scaffolded with `tools/new_plugin.py Glue`.

| Piece | File |
|---|---|
| Parameter table | `src/GlueParams.h` |
| DSP core (P1 contract) | `src/GlueCore.{h,cpp}` |
| cuif UI (shared by both editors) | `src/GlueUi.{h,cpp}` |
| LPI build (P2 template) | `src/lpi_glue.cpp` |
| VST3 build (VST3 SDK, `VSTs/common/vst3`) | `src/vst3_glue.cpp` |
| Tests | `src/DSPTests.cpp` (generic kit + own), `test/vst3_glue_tests.cpp`, `test/lpi_glue_gui_harness.c`, `test/golden/` |

## Signal path

The whole loop runs at 4× the sample rate (`loudio::Oversampler<4>`),
stereo-linked: the VCA multiplies the input by the gain decided one loop
sample earlier, a `tanh` rail (Vmax 4: 3rd harmonic −46 dB at 0 dBFS)
bends it, and the sidechain taps that **output**: optional high-pass,
rectify, dB, feedback gain computer `(R − 1) × max(0, level − T)` (so the
in→out slope is 1/R), then the timing network: an attack one-pole and a
release one-pole from the switches, or **Auto** = the maximum of a fast
follower (100 ms release) and a slow one (300 ms attack, 1.2 s release),
the hardware's two-capacitor behaviour. Back at the base rate: makeup and
a dry/wet mix against the input delayed by the oversampler's 48 samples,
which is the plugin's fixed latency.

## Parameters

| id | name | positions / range | default |
|---|---|---|---|
| `threshold` | Threshold | −40..0 dB | −10 |
| `ratio` | Ratio | 2:1 / 4:1 / 10:1 | 4:1 |
| `attack` | Attack | 0.1 / 0.3 / 1 / 3 / 10 / 30 ms | 10 ms |
| `release` | Release | 0.1 / 0.2 / 0.4 / 0.8 / 1.6 s / Auto | Auto |
| `makeup` | Makeup | 0..24 dB | 0 |
| `scHpf` | Sidechain HPF | 0..500 Hz | 0 (off) |
| `mix` | Mix | 0..100 % | 100 |

## UI (700 × 300)

Input / GR / output meters (the GR meter is the bus compressor's VU),
four knobs, three switch dropdowns. One `'M'` meter frame per block on
VST3; LPI reads before render.

## Tests

Beyond the generic kit: 10 s of a 0 dBFS square at 10:1 is finite,
bounded, free of DC drift, with the gain reduction steady within 0.05 dB
(no limit cycle); the three ratio switches give the feedback ratios
within 0.3 dB; attack sharpened by the loop gain, release is the switch's
one-pole (37% at 100 ms), auto release is program-dependent; the rail's
aliasing products on a 19 kHz tone are >60 dB down at 4× while its 3rd
harmonic on 1 kHz is at −46 dB; latency 48, mix 0 bit-exact delayed dry;
golden IRs `default`, `ratio4`, `ratio10mix`.

Build (Windows, from the repo root):

    cmake -S VSTs/Glue -B build/glue
    cmake --build build/glue --config Debug --target dsp_tests lpi_glue lpi_glue_gui_harness lpi_conformance

VST3 (Windows, macOS or Linux):

    cmake -S VSTs/Glue -B build/glue-vst3 -DCMAKE_BUILD_TYPE=Debug
    cmake --build build/glue-vst3 --config Debug --target LoudioGlue_VST3_validate vst3_glue_tests

DSP tests only (any platform):

    cmake -S VSTs/Glue -B build/glue-tests -DLOUDIO_PLUGIN_TESTS_ONLY=ON
    cmake --build build/glue-tests --target dsp_tests && ./build/glue-tests/dsp_tests
