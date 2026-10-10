# Loudio Bus

Channel utility (#141) on the plugin platform (#125): gain, pan, width,
phase invert per channel, L/R swap, mono sum, DC blocker. "Bus" per the
spike's §5.1 reading (a bus *compressor* is the Glue; routing is
host-side). Scaffolded with `tools/new_plugin.py Bus`.

| Piece | File |
|---|---|
| Parameter table | `src/BusParams.h` |
| DSP core (P1 contract) | `src/BusCore.{h,cpp}` |
| cuif UI (shared by both editors) | `src/BusUi.{h,cpp}` |
| LPI build (P2 template) | `src/lpi_bus.cpp` |
| VST3/Standalone build (P3 templates) | `src/PluginProcessor.{h,cpp}` |
| Tests | `src/DSPTests.cpp` (generic kit + own), `test/lpi_bus_gui_harness.c`, `test/golden/` |

## Signal path

Per sample: swap L/R → phase invert per channel → mono sum → width
(mid/side, side scaled 0..2) → pan (constant power, `gL = √2 cos t`,
`gR = √2 sin t`: unity at centre, +3 dB on the kept side at a hard pan,
so centre is −3 dB per side relative to hard) → gain → optional 5 Hz DC
blocker. Gain, pan and width ramp over 20 ms. With every parameter at
its default the Bus is a **bit-exact passthrough** (the width and pan
stages are skipped at exactly neutral). No latency.

## Parameters

| id | name | range | default |
|---|---|---|---|
| `gain` | Gain | −60..+12 dB | 0 |
| `pan` | Pan | −100..100 | 0 |
| `width` | Width | 0..200 % | 100 |
| `phaseL` / `phaseR` | Phase L / R | off / on | off |
| `swap` | Swap L/R | off / on | off |
| `mono` | Mono | off / on | off |
| `dcBlock` | DC Block | off / on | off |

## UI (560 × 260)

Input and output meters, three knobs, five toggle buttons. A dedicated
pan/width control (P5 item 5) can replace the two knobs later.

## Tests

Beyond the generic kit: neutral settings are bit-exact; the pan law is
constant power with centre unity and a hard pan +3 dB / silent; width 0
gives identical channels and 200 doubles the side; both phases inverted
sum to exactly zero against the input; swap and mono exact; gain ramps
click-free and lands exactly; the DC blocker removes DC within a second
and passes 1 kHz within 0.05 dB; golden IRs `default` and `shaped`.

Build (Windows, from the repo root):

    cmake -S VSTs/Bus -B build/bus
    cmake --build build/bus --config Debug --target dsp_tests lpi_bus lpi_bus_gui_harness lpi_conformance
    cmake --build build/bus --config Debug --target LoudioBus_VST3 LoudioBus_Standalone

DSP tests only (any platform):

    cmake -S VSTs/Bus -B build/bus-tests -DLOUDIO_PLUGIN_TESTS_ONLY=ON
    cmake --build build/bus-tests --target dsp_tests && ./build/bus-tests/dsp_tests
