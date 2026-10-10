# Loudio Compressor

Feed-forward dynamic range compressor (#137) on the plugin platform (#125),
after the blueprint in `Plugins/Compressors/Compressor/README.md` and
Giannoulis, Massberg & Reiss, *Digital Dynamic Range Compressor Design*
(2012). Scaffolded with `tools/new_plugin.py Compressor`.

| Piece | File |
|---|---|
| Parameter table | `src/CompressorParams.h` |
| DSP core (P1 contract) | `src/CompressorCore.{h,cpp}` |
| cuif UI (shared by both editors) | `src/CompressorUi.{h,cpp}` |
| LPI build (P2 template) | `src/lpi_compressor.cpp` |
| VST3/Standalone build (P3 templates) | `src/PluginProcessor.{h,cpp}` |
| Tests | `src/DSPTests.cpp` (generic kit + own), `test/lpi_compressor_gui_harness.c`, `test/golden/` |

## Signal path

Per sample, stereo-linked:

1. **Sidechain**: a copy of the undelayed input, optionally through a
   2nd-order Butterworth high-pass (`Sidechain HPF`, 0 = off) so bass does
   not pump the compressor.
2. **Detector**: `Peak` takes max(|L|, |R|) with no ballistics of its own;
   `RMS` is a 20 ms one-pole mean square (a sine reads 3 dB lower).
3. **Gain computer**: `loudio::GainComputer`, quadratic soft knee of
   `Knee` dB centred on `Threshold`, slope 1/`Ratio` above it.
4. **Smoothing** in the dB domain, Giannoulis' smooth decoupled follower:
   a release-only stage `y1 = max(target, aR·y1 + (1−aR)·target)` feeding
   an attack stage `y = aA·y + (1−aA)·y1`. A step in gain reduction reaches
   63% in one `Attack`, and decays to 37% in one `Release`.
5. **Gain**: `10^(−y/20) × makeup` on the input delayed by `Lookahead`,
   so the gain change arrives with (or before) the transient. The
   lookahead is the reported latency (`lpi.latency.v1` /
   `setLatencySamples`) and follows the parameter immediately.
6. **Mix**: dry/wet in linear amplitude, both on the delayed signal, so
   mix 0 is bit-exact dry.

`Auto Makeup` replaces the `Makeup` knob with the reduction a 0 dBFS
signal would get (`−T·(1 − 1/R)` for a hard knee), so unity-level
material comes out at unity. Makeup and mix changes ramp over 20 ms.

No oversampling in v1 (per #137: measure the gain-modulation aliasing
before adding `Oversampler`).

## Parameters

| id | name | range | default |
|---|---|---|---|
| `threshold` | Threshold | −60..0 dB | −20 |
| `ratio` | Ratio | 1..20 | 4 |
| `knee` | Knee | 0..24 dB | 6 |
| `attack` | Attack | 0.1..200 ms | 10 |
| `release` | Release | 10..2000 ms | 100 |
| `lookahead` | Lookahead | 0..10 ms | 0 |
| `makeup` | Makeup | 0..24 dB | 0 |
| `mix` | Mix | 0..100 % | 100 |
| `detector` | Detector | Peak / RMS | Peak |
| `autoMakeup` | Auto Makeup | off / on | off |
| `scHpf` | Sidechain HPF | 0..500 Hz | 0 (off) |

## UI (920 × 420)

Transfer curve (in → out dB, −60..0) with the live operating point;
input, gain-reduction and output meters; knobs in two rows; detector
dropdown and auto-makeup toggle. The core publishes per-block peak
in/out levels and the block's deepest gain reduction; the VST3 wrapper
ships them as one `'M'` frame per block, the LPI wrapper reads them
before each render.

## Tests

Beyond the generic kit: −20 dBFS sine at threshold −30, 4:1 →
−27.5 dBFS within 0.05 dB; soft-knee value at the threshold and hard
curve at the knee edges; attack to 63% and release to 37% in one time
constant (DC steps, read off the GR meter); `latencySamples()` equals
the lookahead in samples, the audio is delayed by exactly that, and a
transient arrives already attenuated; mix 0 bit-exact dry, mix 50 the
linear midpoint; RMS reads 3 dB lower; auto makeup; sidechain HPF; the
meter readings; and golden impulse responses for `default`, `heavy` and
`rms`.

Build (Windows, from the repo root):

    cmake -S VSTs/Compressor -B build/compressor
    cmake --build build/compressor --config Debug --target dsp_tests lpi_compressor lpi_compressor_gui_harness lpi_conformance
    cmake --build build/compressor --config Debug --target LoudioCompressor_VST3 LoudioCompressor_Standalone

DSP tests only (any platform):

    cmake -S VSTs/Compressor -B build/compressor-tests -DLOUDIO_PLUGIN_TESTS_ONLY=ON
    cmake --build build/compressor-tests --target dsp_tests && ./build/compressor-tests/dsp_tests
