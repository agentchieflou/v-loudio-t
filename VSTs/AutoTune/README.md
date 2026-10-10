# Loudio AutoTune

Real-time pitch correction (#146) on the plugin platform (#125): YIN
pitch tracking, key/scale quantisation with retune speed and humanise,
TD-PSOLA resynthesis with a formant shift. Blueprint:
`Plugins/AutoTune/README.md` (amended at its top). Scaffolded with
`tools/new_plugin.py AutoTune`.

| Piece | File |
|---|---|
| Parameter table | `src/AutoTuneParams.h` |
| DSP core (P1 contract) | `src/AutoTuneCore.{h,cpp}` |
| Pitch detector / shifter | `DSP/include/loudio/dsp/PitchDetectorYin.h`, `PsolaShifter.h` (new) |
| cuif UI (shared by both editors) | `src/AutoTuneUi.{h,cpp}` |
| LPI build (P2 template) | `src/lpi_autotune.cpp` |
| VST3/Standalone build (P3 templates) | `src/PluginProcessor.{h,cpp}` |
| Tests | `src/DSPTests.cpp` (generic kit + own), `test/lpi_autotune_gui_harness.c`; no golden IR (time-variant, exempt) |

## Signal path

Every 256-sample hop the mono sum's last 1024 + 800 samples go through
`PitchDetectorYin` (threshold 0.15, parabolic lag interpolation). A
voiced frame (RMS above −50 dBFS, aperiodicity under 0.35) gives f0 →
MIDI against the reference A4 → the nearest note of the key/scale → a
correction in cents × (1 − humanise), smoothed by the retune speed
(instant on a new note or at 0 ms), and handed to one `PsolaShifter`
per channel as period = fs/f0 and ratio = 2^(cents/1200). Unvoiced
frames pass through the shifters unchanged (pseudo-period OLA sums to
exactly 1). Mix blends with the dry input delayed to match. Latency is
fixed: 2048 + fs/60 samples (2848 at 48 kHz), the analysis window.

## Parameters

| id | name | range | default |
|---|---|---|---|
| `key` | Key | C … B | C |
| `scale` | Scale | Chromatic, Major, Minor, Harm. minor, Dorian, Pent. major, Pent. minor, Blues | Chromatic |
| `retuneSpeed` | Retune Speed | 0..400 ms | 20 |
| `humanise` | Humanise | 0..100 % | 0 |
| `formant` | Formant | −12..12 st | 0 |
| `inputType` | Input Type | Soprano / Alto / Tenor / Bass / Instrument | Instrument |
| `mix` | Mix | 0..100 % | 100 |
| `reference` | Reference | 415..466 Hz | 440 |

## UI (660 × 240)

Key, scale and input-type dropdowns, a detected-note readout
("A4 +12c", a mouse-disabled button), five knobs, a correction meter in
cents. A dedicated pitch display and scale editor are follow-ups.

## Tests

Generic kit (no golden IR) plus: YIN within 1 cent from 110 to 880 Hz on
harmonic tones and noise unvoiced; 445 / 436 / 440 Hz with retune 0 →
440 Hz within 1 cent at the input's level within 1 dB; key/scale
quantisation (C major, A minor pentatonic, E major) and the reference
pitch; retune speed glides with its time constant, humanise keeps part
of the deviation; latency equals the analysis window, mix 0 is the
bit-exact delayed dry, unvoiced material passes through within 1e-4;
formant shifts stay bounded.

Build (Windows, from the repo root):

    cmake -S VSTs/AutoTune -B build/autotune
    cmake --build build/autotune --config Debug --target dsp_tests lpi_autotune lpi_autotune_gui_harness lpi_conformance
    cmake --build build/autotune --config Debug --target LoudioAutoTune_VST3 LoudioAutoTune_Standalone

DSP tests only (any platform):

    cmake -S VSTs/AutoTune -B build/autotune-tests -DLOUDIO_PLUGIN_TESTS_ONLY=ON
    cmake --build build/autotune-tests --target dsp_tests && ./build/autotune-tests/dsp_tests
