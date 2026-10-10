# Plugin platform research spike

**Purpose.** Before building the next ten plugins, work out what a plugin costs today, what is generic, what must be lifted into shared infrastructure first, and what each planned plugin needs on top. This is the design behind the "Plugin platform + plugin catalogue" epic; each section maps to one or more of its issues.

**Status.** Research + plan, grounded in the Reverb as it exists on `main` after #119–#124. Numbers are measured, not estimated, unless marked.

**Issue map.** Epic #125. Platform: P1 #127, P2 #128, P3 #129, P4 #130, P5 #131, P6 #132, P7 #133, P8 #134, P9 #135. Plugins: EQ-8 #136, Compressor #137, Gate #138, Limiter #139, Glue #140, Bus #141, Delay #142, Flanger #143, Phaser #144, EQ-3 #145, AutoTune #146, candidates #147.

---

## 1. What one plugin costs today (Reverb, measured)

| Layer | Files | Lines | Generic? |
|---|---|---|---|
| DSP engine | `ReverbCore.{h,cpp}` | 634 | No — but ~120 of those lines (param queue drain, `setParameterValue(s)`, `getState`/`setState`, analyzer dirty flags) are copy-paste for any plugin |
| Parameter table | `SharedStructures.h` | 85 | Pattern is generic (`ParamMeta` + `kReverbParams[]`), contents are not |
| State blob v1 | in `ReverbCore.cpp` | ~90 | **Fully generic** (magic + version + id-keyed entries) |
| Editor (shared tree) | `ReverbUi.{h,cpp}`, `ReverbUiMapping.h`, `ReverbLayout.h`, `ReverbThemeMapping.h` | 383 | Callback contract (`sendParams`) and the value mapping are generic; the widget list is not |
| VST3 wrapper | `PluginProcessor.{h,cpp}`, `PluginEditor.{h,cpp}` | 466 | **~90% generic**: APVTS layout from the table, UI→DSP ring bridge, analyzer ring, timer, DPI debounce |
| LPI wrapper | `lpi_reverb.cpp` | 314 | **~93% generic**: only 23 lines name anything Reverb-specific (ids, panel size, the `ReverbUi` calls) |
| Tests | `DSPTests.cpp`, `lpi_reverb_gui_harness.c`, goldens | 1075 + 320 KB | Golden-IR runner, alloc hook, state tests, harness skeleton are generic; the assertions are not |
| Build | `CMakeLists.txt` | 138 | Generic apart from names |
| Shared DSP (since #124) | `DSP/include/loudio/dsp/*` | 660 | Yes |

**Takeaway:** of ~3,100 plugin lines, roughly 1,000 are plugin-specific DSP/UI/tests and ~2,100 are scaffolding that every plugin would re-copy. Copying it ten times means ten places to fix the next #119-class bug. The platform issues below remove the copying first.

## 2. Shared infrastructure to build before plugin #2

Ordered by dependency. P1–P4 are prerequisites for any new plugin; P5–P9 are pulled in by the first plugin that needs them.

| # | Deliverable | Extracted from / rationale |
|---|---|---|
| **P1** | `loudio::ParamTable` (id, name, range, default, step, `LPI_PARAM_*` flags), `loudio::StateBlob` (the v1 format, magic per plugin), `loudio::ParamQueue` (ring + all-or-nothing batch enqueue + drain), `paramToWidget`/`widgetToParam` | `SharedStructures.h`, `ReverbCore.cpp` state + queue, `ReverbUiMapping.h`. A plugin core becomes: table + `prepare/process` + whatever it draws. |
| **P2** | LPI wrapper template `PluginABI/wrapper/lpi/plugin_template.h`: given a Core (P1 contract) and a UI builder, provides the whole `lpi_plugin_api`, `gui.offscreen.v1`, `latency.v1`, `params.changes.v1` (gesture tracking included) | `lpi_reverb.cpp` minus its 23 specific lines |
| **P3** | VST3 common `VSTs/common/`: `LoudioProcessor<Core>` (APVTS layout from the table, cached raw-value pointers, ring bridge, analyzer FIFO) and `LoudioEditor<Ui>` (cuif child window, timer, DPI debounce, spectrum poll) | `PluginProcessor`/`PluginEditor` |
| **P4** | Plugin scaffold: `tools/new_plugin.py <Name>` emits table/core/ui/both wrappers/tests/goldens/CMake; the generic core test kit `DSP/include/loudio/testing/CoreTests.h`; CI matrix from `tools/list_plugins.py` runs `dsp_tests`, `lpi_conformance`, the GUI harness and the VST3 + Standalone build per plugin (Windows) plus every plugin's `dsp_tests` on Linux; JUCE submodule shared at `VSTs/JUCE` | A generated plugin is green on CI before any DSP is written |
| **P5** | cuif widgets the new UIs need: level/gain-reduction meter (peak + RMS, ballistics in the widget), transfer-curve display (compressor/gate/limiter), frequency-response curve with draggable band handles (EQ-8; bezier editor is 60% there), numeric readout + text entry (every plugin), stereo pan/width control (Bus). Keyboard focus/text input plumbing comes with the readout. | Widget set is knob/slider/button/dropdown/bezier/analyzer/tabbar |
| **P6** | Dynamics DSP in `loudio_dsp`: peak/RMS detector with attack/release/hold (extend `EnvelopeDetector`), soft-knee gain computer (dB domain), lookahead (on `DelayLine`), sidechain HPF (on `SimpleBiquad`), dB↔linear helpers, hysteresis gate stage | Compressor, Gate, Limiter, Glue, De-esser all share these |
| **P7** | Modulation DSP: LFO (sine/tri/saw/S&H, phase offset per channel, free-running or tempo-synced), modulated fractional delay (on `DelayLine::read(float)`), first-order all-pass section + N-stage chain, feedback path with soft clip | Delay, Flanger, Chorus, Phaser, Tremolo |
| **P8** | Oversampling: 2×/4× polyphase (half-band IIR or FIR), JUCE-free so the LPI build has it too; latency reported through `lpi.latency.v1` | Glue and Limiter saturation, Saturator. The blueprints assumed `juce::dsp::Oversampling`, which the LPI path cannot use. |
| **P9** | `lpi.transport.v1` extension (tempo, PPQ position, playing/looping flags), `loudio::TransportInfo` mirror + division helpers, detected `Core::setTransport` in both wrappers | Tempo-synced Delay and LFO rates. Shape agreed with the GhostDAW session (#135): it fills tempo/PLAYING/ppq for browser tracks, tempo-only for Strudel buses; plugins keep a free-running fallback. |

## 3. Plugin catalogue

Sizes: S ≈ 1 week-equivalent of the Reverb's DSP effort, M ≈ 2, L ≈ 4, XL ≈ 8+. "Reuses" lists existing `loudio_dsp` blocks; "needs" lists platform issues.

| Plugin | Topology (one line) | Reuses | Needs | Params | UI | Tests beyond the standard set | Size |
|---|---|---|---|---|---|---|---|
| **EQ-8** (spec: `Plugins/EQ/EQ-8`) | 8 cascaded biquads, per band type/freq/gain/Q, parallel FFT analyzer | `SimpleBiquad` (low/high shelf + peaking already there; add HP/LP/notch), `FFTAnalyzer` | P1–P4, P5 (curve + readout) | 8×4 + global gain/bypass ≈ 34 | Frequency-response curve with draggable nodes over the analyzer | Golden IRs per band type; measured magnitude at 1 kHz vs. the coefficient formula within 0.1 dB | M |
| **Compressor** (spec: `Plugins/Compressors/Compressor`) | Feed-forward: detector → soft-knee gain computer → attack/release → lookahead-delayed multiply | `DelayLine`, `EnvelopeDetector` | P1–P4, P5 (GR meter, transfer curve), P6 | threshold, ratio, knee, attack, release, lookahead, makeup, mix, detector mode ≈ 10 | Transfer curve, GR meter, in/out meters | Static ratio test (−20 dB in, 4:1 above −30 dB threshold → exactly −27.5 dB out at steady state); attack/release time-constant test; lookahead latency reported = delay | M |
| **Gate** | Detector → hysteresis threshold (open/close) → attack/hold/release → multiply; optional range (not −∞) and sidechain HPF | `EnvelopeDetector`, `SimpleBiquad` | P6 | threshold, hysteresis, attack, hold, release, range, HPF ≈ 8 | GR meter, threshold/open indicator | Burst-silence test (opens within attack, closes after hold+release); hysteresis test (no chatter at threshold ±1 dB) | S |
| **Limiter** | Lookahead peak detection → instantaneous gain computer (ceiling) → release → multiply; optional 2×/4× oversampled true-peak | `DelayLine` | P6, P8 | ceiling, release, lookahead, oversampling, link ≈ 6 | GR meter, ceiling readout | **Hard guarantee test**: no output sample exceeds ceiling on a worst-case burst; true-peak within 0.1 dB with oversampling | M |
| **Glue** (spec: `Plugins/Compressors/Glue`) | *Feedback* topology: VCA cell → output → sidechain HPF → log detector → gain computer back into the VCA; `tanh` saturation; oversampled | `EnvelopeDetector`, `SimpleBiquad` | P6, P8; **decision needed** on the blueprint's WDF/Eigen choices (§5) | threshold, ratio (2/4/10), attack, release (incl. auto), makeup, HPF, mix ≈ 8 | GR meter, VU-style display | One-sample delayed feedback test (stable, no limit cycle on a 0 dBFS square); null test vs. golden at ratio 4 | L |
| **Delay** | Stereo delay lines with feedback, ping-pong option, modulation, filtered feedback, tempo sync | `DelayLine`, `SimpleBiquad`, `OnePoleLPF` | P7, **P9** for sync | time L/R, sync divisions, feedback, cross-feed, filter, mod rate/depth, mix ≈ 11 | Time readouts, sync dropdown, feedback meter | Impulse → echo positions exactly at N samples; feedback decay matches −feedback dB per repeat; sync division → time at a given tempo | M |
| **Flanger** | Short modulated delay (0.1–10 ms) with feedback, through-zero option, LFO | `DelayLine` | P7 | rate, depth, feedback, delay, stereo phase, mix ≈ 7 | LFO shape display | Notch spacing test: with delay d, comb notches at odd multiples of 1/(2d) | S |
| **Phaser** | N (4–12) first-order all-pass stages, LFO-swept corner, feedback | — | P7 | stages, rate, depth, centre, feedback, stereo phase, mix ≈ 8 | Stage count dropdown, LFO display | All-pass unity-magnitude test; N stages → N/2 notches | S |
| **Bus** (utility; interpretation: see §5) | Gain, pan law, phase invert, L/R swap, mono, width (M/S), DC block | — | P5 (pan/width control) | gain, pan, width, phase L/R, mono, swap ≈ 7 | Pan/width control, meters | Pan-law test (−3 dB centre); width 0 → identical channels; phase invert null | S |
| **EQ-3** (spec: `Plugins/EQ/EQ-3`) | Linkwitz-Riley 3-band split, per-band gain to −∞ (kill), sum | `Crossover` (first-order today; LR4 needs 2 cascaded biquads) | P1–P4 | low/mid/high gain, 2 crossover freqs, kill switches ≈ 8 | Three faders + kill buttons | Flat-sum test (all 0 dB → null against input within 0.01 dB) | S |
| **AutoTune** (spec: `Plugins/AutoTune`) | YIN pitch tracking → scale quantisation → TD-PSOLA resynthesis | `FFTAnalyzer` (partially) | P1–P4; needs its own pitch/PSOLA blocks; latency reporting | key, scale, retune speed, humanise, formant ≈ 8 | Pitch display, scale editor | Detected f0 on synthetic tones within 1 cent; latency reported; vocal null test is *not* possible (deliberately not golden-IR) | XL |
| **Candidates** (one triage issue) | Chorus (P7), Tremolo (P7), Saturator (P8), De-esser (P6 + `Crossover`), Stereo Imager (Bus + P5), Transient Shaper (P6) | — | as noted | — | — | — | S–M each |

**What every plugin gets for free once P1–P4 land:** VST3 + LPI builds, id-keyed presets, RT-safe parameter queue, gesture-aware editor change reporting, latency reporting, `dsp_tests` with golden-IR + zero-allocation + state tests, LPI conformance, the offscreen GUI harness with frame-cost numbers, and both CI jobs.

## 4. Build order

The order is chosen so each plugin introduces at most one new shared block and the riskiest shared blocks (P8 oversampling, P9 transport) are proven on the smallest plugin that needs them.

1. **P1 → P2 → P3 → P4** (platform; P2 and P3 can be built in parallel once P1 exists). Validation: the Reverb is re-targeted onto them with *no* behaviour change — the golden IRs, the state tests and the LPI harness must pass unchanged. That is the acceptance test for the platform.
2. **EQ-8** — first new plugin; lowest DSP risk (biquads exist), exercises P5's curve widget and the scaffold end to end.
3. **Compressor** — introduces P6 and the GR meter.
4. **Gate** — P6 reuse; small.
5. **Limiter** — introduces P8 on the simplest saturation-free use (true-peak), with the hard ceiling guarantee as its test.
6. **Glue** — P6 + P8 + feedback topology; the blueprint's WDF/Eigen decision (§5) must be made first.
7. **Bus** — small, needs only P5's pan/width control; can slot anywhere.
8. **P9 → Delay** — tempo sync needs the transport extension; coordinate with noprod.
9. **Flanger → Phaser → (Chorus, Tremolo)** — P7 reuse chain.
10. **EQ-3** — small; after EQ-8 so the LR4 crossover upgrade of `Crossover` is done once.
11. **AutoTune** — last; XL and mostly bespoke DSP.

## 5. Decisions needed

1. **"Bus".** Interpreted as a utility/channel tool (gain, pan, width, phase, mono). If it meant a bus *compressor*, that is Glue. If it meant a summing/routing device, that is host-side (noprod insert chains), not a plugin.
2. **Glue blueprint vs. platform constraints.** The blueprint prescribes Wave Digital Filters, Eigen for state-space maths and `juce::dsp::Oversampling`. Eigen and JUCE DSP are unavailable in the LPI build, and WDF for a VCA compressor is heavy for the sonic benefit. **Decided (with #140's implementation):** the same feedback topology and `tanh` VCA, resolved with a one-sample-delayed feedback loop running at 4× on our own oversampler (P8). The blueprint carries the amendment at its top.
3. **Precision.** The EQ-8 blueprint asks for 64-bit biquads for low-frequency bands. Proposal: `SimpleBiquad` gets a `double`-state variant (TDF2) used by EQ-8 and EQ-3 only; everything else stays `float`.
4. **SIMD.** All five blueprints list AVX/NEON intrinsics. Proposal: none in v1 — the Reverb's `process()` is under 5% of a core at 48 kHz/512 and these plugins are cheaper; revisit with a measurement, as with #48.
5. **Tempo sync.** Needs P9 and noprod's cooperation. If noprod won't carry transport in its v1, the Delay ships free-running only.
6. **Scaffold language.** Decided (P4, #130): `tools/new_plugin.py`, Python 3 with no dependencies. CI needs Python anyway to enumerate plugins for the matrix (`tools/list_plugins.py`), and both GitHub runners ship it; contributors only need it to scaffold, never to build.

## 6. Non-goals (this epic)

Sidechain/external inputs (LPI has no bus concept; Compressor/Gate use internal detection only in v1), MIDI, GUI resizing/reflow (the layout helper still ignores height), keyboard shortcuts beyond text entry, and any new plugin *format*.
