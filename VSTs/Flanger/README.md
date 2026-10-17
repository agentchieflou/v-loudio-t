# Loudio Flanger

Flanger (#143) on the plugin platform (#125): a short modulated delay
with ± feedback, stereo LFO phase offset and a through-zero mode.
Blueprint: `Plugins/Modulation/Flanger/README.md`. Scaffolded with
`tools/new_plugin.py Flanger`.

| Piece | File |
|---|---|
| Parameter table | `src/FlangerParams.h` |
| DSP core (P1 contract) | `src/FlangerCore.{h,cpp}` |
| cuif UI (shared by both editors) | `src/FlangerUi.{h,cpp}` |
| LPI build (P2 template) | `src/lpi_flanger.cpp` |
| VST3 build (VST3 SDK, `VSTs/common/vst3`) | `src/vst3_flanger.cpp` |
| Tests | `src/DSPTests.cpp` (generic kit + own), `test/vst3_flanger_tests.cpp`, `test/lpi_flanger_gui_harness.c`, `test/golden/` |

## Signal path

Per channel: `ModulatedDelay` at `centre × (1 + depth·LFO)` with its own
tanh-clipped ± feedback, mixed with the dry input (or, with through-zero
on, with the dry input delayed by the centre so the sweep crosses it).
The right channel reads the sine LFO `stereoPhase` degrees behind the
left. Depth and mix ramp over 20 ms, the centre inside the line. No
reported latency.

## Parameters

| id | name | range | default |
|---|---|---|---|
| `rate` | Rate | 0.05..10 Hz | 0.3 |
| `depth` | Depth | 0..100 % of centre | 50 |
| `centre` | Centre | 0.1..10 ms | 3 |
| `feedback` | Feedback | −95..95 % | 30 |
| `stereoPhase` | Stereo Phase | 0..180 ° | 90 |
| `throughZero` | Through Zero | off / on | off |
| `mix` | Mix | 0..100 % | 50 |

## UI (580 × 220)

Six knobs in a row and a THROUGH ZERO toggle.

## Tests

Beyond the generic kit: the static comb's notches at odd multiples of
187.5 Hz (centre 128 samples), > 20 dB deep on a 4096-point FFT of white
noise, inverted by negative feedback; feedback 90 % with full-depth
modulation on noise stays finite and bounded; stereo phase 180° puts the
right channel's delay trajectory exactly half a period behind the left
(impulse-train centroids), 0° keeps them identical; through-zero with
depth 0 is a pure centre delay; mix 0 bit-exact dry; golden IRs
`default`, `throughzero`.

Build (Windows, from the repo root):

    cmake -S VSTs/Flanger -B build/flanger
    cmake --build build/flanger --config Debug --target dsp_tests lpi_flanger lpi_flanger_gui_harness lpi_conformance

VST3 (Windows, macOS or Linux):

    cmake -S VSTs/Flanger -B build/flanger-vst3 -DCMAKE_BUILD_TYPE=Debug
    cmake --build build/flanger-vst3 --config Debug --target LoudioFlanger_VST3_validate vst3_flanger_tests

DSP tests only (any platform):

    cmake -S VSTs/Flanger -B build/flanger-tests -DLOUDIO_PLUGIN_TESTS_ONLY=ON
    cmake --build build/flanger-tests --target dsp_tests && ./build/flanger-tests/dsp_tests
