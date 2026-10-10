# Loudio Gate

Noise gate / downward expander (#138) on the plugin platform (#125).
Blueprint: `Plugins/Dynamics/Gate/README.md`. Scaffolded with
`tools/new_plugin.py Gate`.

| Piece | File |
|---|---|
| Parameter table | `src/GateParams.h` |
| DSP core (P1 contract) | `src/GateCore.{h,cpp}` |
| cuif UI (shared by both editors) | `src/GateUi.{h,cpp}` |
| LPI build (P2 template) | `src/lpi_gate.cpp` |
| VST3/Standalone build (P3 templates) | `src/PluginProcessor.{h,cpp}` |
| Tests | `src/DSPTests.cpp` (generic kit + own), `test/lpi_gate_gui_harness.c`, `test/golden/` |

## Signal path

Per sample, stereo-linked: an optional sidechain high-pass, a fast peak
detector (near-instant attack, 5 ms release) on max(|L|, |R|), a
hysteresis switch (open above `Threshold`, close below `Threshold −
Hysteresis`), a `Hold` timer that defers closing (and restarts on any
re-open), then a linear one-pole gain toward 1 (`Attack`) or the
`Range` floor (`Release`), applied to the delayed input. The audio is
always delayed by the maximum lookahead (10 ms) and the detector level by
the difference, so the gate opens `Lookahead` ms early while the reported
latency stays fixed for the life of an activation (the LPI contract).

## Parameters

| id | name | range | default |
|---|---|---|---|
| `threshold` | Threshold | −80..0 dB | −40 |
| `hysteresis` | Hysteresis | 0..24 dB | 6 |
| `attack` | Attack | 0.01..100 ms | 1 |
| `hold` | Hold | 0..1000 ms | 50 |
| `release` | Release | 1..2000 ms | 100 |
| `range` | Range | −80..0 dB | −80 (silence) |
| `scHpf` | Sidechain HPF | 0..500 Hz | 0 (off) |
| `lookahead` | Lookahead | 0..10 ms | 0 |

## UI (600 × 260)

Input and gain-reduction meters, an OPEN lamp (a mouse-disabled toggle
button mirroring the gate state), eight knobs in two rows. The core
publishes per-block input peak, deepest gain reduction and the open
state; the VST3 wrapper ships one `'M'` frame per block, the LPI
wrapper reads them before each render.

## Tests

Beyond the generic kit: a −20 dBFS burst opens to 63% within the attack
time, stays open through the hold and reaches 37% one release after the
hold (plus the detector's 20 dB decay); ±1 dB around the threshold gives
one transition with 6 dB hysteresis and chatter with none; range −20 dB
leaves exactly 0.1 × input when closed and −80 dB is silence;
`latencySamples()` is the fixed 10 ms maximum with the audio delayed by
that and, with lookahead set, a transient arriving already open; the sidechain HPF keeps 30 Hz
rumble from opening the gate; golden IRs `default` and `expander`.

Build (Windows, from the repo root):

    cmake -S VSTs/Gate -B build/gate
    cmake --build build/gate --config Debug --target dsp_tests lpi_gate lpi_gate_gui_harness lpi_conformance
    cmake --build build/gate --config Debug --target LoudioGate_VST3 LoudioGate_Standalone

DSP tests only (any platform):

    cmake -S VSTs/Gate -B build/gate-tests -DLOUDIO_PLUGIN_TESTS_ONLY=ON
    cmake --build build/gate-tests --target dsp_tests && ./build/gate-tests/dsp_tests
