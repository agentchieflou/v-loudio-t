# Loudio Delay

Stereo / ping-pong delay (#142) on the plugin platform (#125): two delay
lines with independent times or tempo-synced divisions, filtered and
soft-clipped feedback with cross-feed, LFO modulation, dry/wet. Blueprint:
`Plugins/Delay/README.md`. Scaffolded with `tools/new_plugin.py Delay`.

| Piece | File |
|---|---|
| Parameter table | `src/DelayParams.h` |
| DSP core (P1 contract) | `src/DelayCore.{h,cpp}` |
| cuif UI (shared by both editors) | `src/DelayUi.{h,cpp}` |
| LPI build (P2 template) | `src/lpi_delay.cpp` |
| VST3 build (VST3 SDK, `VSTs/common/vst3`) | `src/vst3_delay.cpp` |
| Tests | `src/DSPTests.cpp` (generic kit + own), `test/vst3_delay_tests.cpp`, `test/lpi_delay_gui_harness.c`, `test/golden/` |

## Signal path

Per sample: each line's tap is read (`ModulatedDelay::read`, smoothed
time plus the LFO's modulation, the right side a quarter cycle behind),
filtered by the loop's low cut and high cut (off at their range ends),
scaled by `feedback` and soft clipped (`FeedbackPath`), then written
back into its own line and, scaled by `crossFeed`, into the other one
(`ModulatedDelay::write`). Wet is the taps; mix blends with the dry
input. No latency.

Sync on takes each side's division at the host tempo (`setTransport`,
through `lpi.transport.v1` or the VST3 host's process context); without a valid tempo
the last one (default 120 BPM) stands. Time changes glide over 20 ms.

## Parameters

| id | name | range | default |
|---|---|---|---|
| `timeL` / `timeR` | Time L / R | 1..2000 ms | 375 |
| `sync` | Sync | off / on | off |
| `divisionL` / `divisionR` | Division L / R | 1/32 … 1/1. (15, with T and .) | 1/8. / 1/8 |
| `feedback` | Feedback | 0..110 % | 40 |
| `crossFeed` | Cross Feed | 0..100 % | 0 |
| `lowCut` | Low Cut | 20..2000 Hz (20 = off) | 100 |
| `highCut` | High Cut | 1000..20000 Hz (20000 = off) | 8000 |
| `modRate` | Mod Rate | 0.05..10 Hz | 0.5 |
| `modDepth` | Mod Depth | 0..10 ms | 0 |
| `mix` | Mix | 0..100 % | 35 |

## UI (860 × 320)

Time knobs, SYNC toggle and division dropdowns on row 1; feedback, cross
feed, low cut, high cut, mod rate, mod depth, mix on row 2; in/out
meters. Readouts of the effective (synced) times are a follow-up with the
readout widget.

## Tests

Beyond the generic kit: echoes at exactly 24 000·n samples for 500 ms
with silence between, each −6.02 dB below the last at feedback 50 % (a
full-scale impulse follows the soft clip's iterate), a 20.51 ms time
interpolated with nothing dropped; cross-feed 100 hops sides on every
repeat, 0 keeps them apart; 120 BPM gives 1/4 = 500 ms, 1/8. = 375 ms,
1/8T = 166.67 ms sample-exact, tempo changes and sync-off follow; a
100 → 400 ms jump slides click-free within the glide's bound; the loop
filters darken repeats and leave lows alone; mix 0 bit-exact; 110 %
feedback stays bounded; golden IRs `default`, `short`, `pingpongmod`.

Also touches `DSP/include/loudio/dsp/ModulatedDelay.h`: `process()` is
now `read()` + `write()` so a loop can filter and cross-feed between the
tap and the write.

Build (Windows, from the repo root):

    cmake -S VSTs/Delay -B build/delay
    cmake --build build/delay --config Debug --target dsp_tests lpi_delay lpi_delay_gui_harness lpi_conformance

VST3 (Windows, macOS or Linux):

    cmake -S VSTs/Delay -B build/delay-vst3 -DCMAKE_BUILD_TYPE=Debug
    cmake --build build/delay-vst3 --config Debug --target LoudioDelay_VST3_validate vst3_delay_tests

DSP tests only (any platform):

    cmake -S VSTs/Delay -B build/delay-tests -DLOUDIO_PLUGIN_TESTS_ONLY=ON
    cmake --build build/delay-tests --target dsp_tests && ./build/delay-tests/dsp_tests
