# Loudio Limiter

Lookahead brickwall / true-peak limiter (#139) on the plugin platform
(#125). Blueprint: `Plugins/Dynamics/Limiter/README.md` (the guarantee
proof, true-peak and latency details live there). Scaffolded with
`tools/new_plugin.py Limiter`.

| Piece | File |
|---|---|
| Parameter table | `src/LimiterParams.h` |
| DSP core (P1 contract) | `src/LimiterCore.{h,cpp}` |
| cuif UI (shared by both editors) | `src/LimiterUi.{h,cpp}` |
| LPI build (P2 template) | `src/lpi_limiter.cpp` |
| VST3/Standalone build (P3 templates) | `src/PluginProcessor.{h,cpp}` |
| Tests | `src/DSPTests.cpp` (generic kit + own), `test/lpi_limiter_gui_harness.c`, `test/golden/` |

## Signal path

Input gain, then per channel: the required gain `min(1, ceiling/peak)`
(peak from the sample, or from the 2×/4× upsampled signal for true-peak),
a sliding-window minimum over the lookahead, a box filter of the same
length as the attack ramp, and a release one-pole that can never rise
above the box. Stereo link mixes each channel's gain with the minimum of
both. The gain multiplies the audio delayed by the fixed latency: 10 ms
maximum lookahead + 24 samples of 4× upsampler delay (504 at 48 kHz),
whatever the parameters say; the detected peaks are delayed by the
remainder so the lookahead parameter still works. **No output sample ever
exceeds the ceiling**, at any block size, in any mode.

## Parameters

| id | name | range | default |
|---|---|---|---|
| `ceiling` | Ceiling | −30..0 dBFS | −0.3 |
| `release` | Release | 1..1000 ms | 100 |
| `lookahead` | Lookahead | 0..10 ms | 2 |
| `oversampling` | Oversampling | Off / 2x / 4x | Off |
| `link` | Link | 0..100 % | 100 |
| `inputGain` | Input Gain | −12..24 dB | 0 |

## UI (620 × 260)

Input, gain-reduction and output meters; five knobs; the oversampling
dropdown. The core publishes per-block input/output peaks and deepest
gain reduction; the VST3 wrapper ships one `'M'` frame per block, the
LPI wrapper reads them before each render.

## Tests

Beyond the generic kit: the hard ceiling guarantee over 72 (mode ×
lookahead × block size) cases of a worst-case burst; true peak within
0.1 dB of the ceiling with 4× on an inter-sample-peak tone (and −3 dB of
overs without); fixed latency of 504 samples in every mode with the
audio delayed by exactly that, and the upsampler delays measured; release
to 63% in one time constant; stereo link; golden IRs `default`,
`truepeak4x`, `unlinked`.

Build (Windows, from the repo root):

    cmake -S VSTs/Limiter -B build/limiter
    cmake --build build/limiter --config Debug --target dsp_tests lpi_limiter lpi_limiter_gui_harness lpi_conformance
    cmake --build build/limiter --config Debug --target LoudioLimiter_VST3 LoudioLimiter_Standalone

DSP tests only (any platform):

    cmake -S VSTs/Limiter -B build/limiter-tests -DLOUDIO_PLUGIN_TESTS_ONLY=ON
    cmake --build build/limiter-tests --target dsp_tests && ./build/limiter-tests/dsp_tests
