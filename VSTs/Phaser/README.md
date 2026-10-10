# Loudio Phaser

Phaser (#144) on the plugin platform (#125): 4/6/8/12 first-order
all-pass stages at a shared corner swept log-wise by an LFO, ± feedback,
stereo LFO offset, dry/wet. Blueprint: `Plugins/Modulation/Phaser/README.md`.
Scaffolded with `tools/new_plugin.py Phaser`.

| Piece | File |
|---|---|
| Parameter table | `src/PhaserParams.h` |
| DSP core (P1 contract) | `src/PhaserCore.{h,cpp}` |
| cuif UI (shared by both editors) | `src/PhaserUi.{h,cpp}` |
| LPI build (P2 template) | `src/lpi_phaser.cpp` |
| VST3/Standalone build (P3 templates) | `src/PluginProcessor.{h,cpp}` |
| Tests | `src/DSPTests.cpp` (generic kit + own), `test/lpi_phaser_gui_harness.c`, `test/golden/` |

## Signal path

Per channel: `corner = centre · 2^(2 · depth · LFO)` (±2 octaves at depth
100 %), the input plus the chain's one-sample-late tanh-clipped feedback
through N `AllPassStage`s at that corner, mixed with the dry signal.
The right channel reads the sine LFO `stereoPhase` degrees behind the
left. Depth, mix and centre ramp (20 / 20 / 50 ms). No latency.

## Parameters

| id | name | range | default |
|---|---|---|---|
| `rate` | Rate | 0.05..10 Hz | 0.4 |
| `depth` | Depth | 0..100 % | 70 |
| `centre` | Centre | 100..5000 Hz | 800 |
| `feedback` | Feedback | −95..95 % | 30 |
| `stereoPhase` | Stereo Phase | 0..180 ° | 90 |
| `stages` | Stages | 4 / 6 / 8 / 12 | 6 |
| `mix` | Mix | 0..100 % | 50 |

## UI (580 × 220)

Six knobs and the stages dropdown.

## Tests

Beyond the generic kit: one all-pass stage is unity magnitude at 100 Hz,
1 kHz and 10 kHz within 0.01 dB; 4 / 6 / 8 / 12 stages give exactly 2 /
3 / 4 / 6 notches on white noise (12 dB deep, 4096-point FFT) at
`800 · tan((2k+1)π/2N)` Hz; feedback 90 % with the full sweep stays finite
and bounded; stereo phase separates the channels and 0° keeps them
identical; mix 0 bit-exact; golden IRs `default`, `twelve`.

Build (Windows, from the repo root):

    cmake -S VSTs/Phaser -B build/phaser
    cmake --build build/phaser --config Debug --target dsp_tests lpi_phaser lpi_phaser_gui_harness lpi_conformance
    cmake --build build/phaser --config Debug --target LoudioPhaser_VST3 LoudioPhaser_Standalone

DSP tests only (any platform):

    cmake -S VSTs/Phaser -B build/phaser-tests -DLOUDIO_PLUGIN_TESTS_ONLY=ON
    cmake --build build/phaser-tests --target dsp_tests && ./build/phaser-tests/dsp_tests
