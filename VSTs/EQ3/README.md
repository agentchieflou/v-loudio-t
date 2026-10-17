# Loudio EQ-3

3-band isolator with kill switches (#145) on the plugin platform (#125):
a 4th-order Linkwitz-Riley split into low / mid / high, per-band gain
from kill (−60 dB) to +12 dB, phase-coherent sum. Blueprint:
`Plugins/EQ/EQ-3/README.md`. Scaffolded with `tools/new_plugin.py EQ3`.

| Piece | File |
|---|---|
| Parameter table | `src/EQ3Params.h` |
| DSP core (P1 contract) | `src/EQ3Core.{h,cpp}` |
| cuif UI (shared by both editors) | `src/EQ3Ui.{h,cpp}` |
| LPI build (P2 template) | `src/lpi_eq3.cpp` |
| VST3 build (VST3 SDK, `VSTs/common/vst3`) | `src/vst3_eq3.cpp` |
| Tests | `src/DSPTests.cpp` (generic kit + own), `test/vst3_eq3_tests.cpp`, `test/lpi_eq3_gui_harness.c`, `test/golden/` |

## Signal path

`loudio::ThreeBandCrossoverLR4` (new, `DSP/include/loudio/dsp/CrossoverLR4.h`,
two cascaded Butterworth `BiquadD` sections per edge in double precision):
the upper crossover splits the input into LP2 and HP2, the lower splits
LP2 into low and mid, and HP2 goes through the lower crossover's
all-pass so all three bands share one phase. Their sum is an all-pass of
unity magnitude; neighbouring bands are in phase at their crossover, so a
kill leaves no notch. Each band is scaled (0 when killed or at −60 dB)
and summed. Gains ramp over 20 ms, crossovers over 5 ms inside the
biquads. No latency. The Reverb's first-order crossover is untouched.

## Parameters

| id | name | range | default |
|---|---|---|---|
| `lowGain` / `midGain` / `highGain` | Low / Mid / High | −60 (kill)..+12 dB | 0 |
| `xoverLowMid` | Low-Mid | 50..1000 Hz | 300 |
| `xoverMidHigh` | Mid-High | 1000..10000 Hz | 3000 |
| `killLow` / `killMid` / `killHigh` | Kill | off / on | off |

## UI (520 × 300)

Three vertical faders with a KILL button under each, two crossover knobs.

## Tests

Beyond the generic kit: every band at 0 dB sums **flat within 0.01 dB
from 20 Hz to 20 kHz** for two crossover settings (the issue's
acceptance point); killing the low band leaves mid + high within 0.1 dB
of flat from 4× the low-mid corner to 20 kHz, including exactly at the
mid-high crossover (no notch), and killing the high band likewise
through the low-mid crossover; ±12 dB per band and the −60 dB / switch
kills; a crossover jump under a 1 kHz sine produces no step beyond the
sine's slope; golden IRs `default`, `dj`.

Build (Windows, from the repo root):

    cmake -S VSTs/EQ3 -B build/eq3
    cmake --build build/eq3 --config Debug --target dsp_tests lpi_eq3 lpi_eq3_gui_harness lpi_conformance

VST3 (Windows, macOS or Linux):

    cmake -S VSTs/EQ3 -B build/eq3-vst3 -DCMAKE_BUILD_TYPE=Debug
    cmake --build build/eq3-vst3 --config Debug --target LoudioEQ3_VST3_validate vst3_eq3_tests

DSP tests only (any platform):

    cmake -S VSTs/EQ3 -B build/eq3-tests -DLOUDIO_PLUGIN_TESTS_ONLY=ON
    cmake --build build/eq3-tests --target dsp_tests && ./build/eq3-tests/dsp_tests
