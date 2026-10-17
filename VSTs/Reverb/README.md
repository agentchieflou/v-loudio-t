# Loudio Reverb

Algorithmic reverb: pre-delay, diffusion, early reflections and an 8-line
FDN with per-line 3-band decay, then post-EQ, width, ducking and gating.
Blueprint: `Plugins/Reverb/README.md`. The first plugin on the platform
(#125) and the first one off JUCE (#171).

| Piece | File |
|---|---|
| Parameter table | `src/SharedStructures.h` |
| DSP core (P1 contract) | `src/ReverbCore.{h,cpp}` |
| cuif UI (shared by both editors) | `src/ReverbUi.{h,cpp}` |
| LPI build (P2 template) | `src/lpi_reverb.cpp` |
| VST3 build (VST3 SDK, `VSTs/common/vst3`) | `src/vst3_reverb.cpp` |
| Tests | `src/DSPTests.cpp`, `test/vst3_reverb_tests.cpp`, `test/lpi_reverb_gui_harness.c`, `test/golden/` |

## VST3

Built on the Steinberg VST3 SDK directly (MIT, `ThirdParty/vst3sdk`), no
JUCE, on Windows, macOS and Linux:

```
cmake -S VSTs/Reverb -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug --target LoudioReverb_VST3_validate vst3_reverb_tests
```

The bundle lands in `build/VST3/<config>/Loudio Reverb.vst3`;
`LoudioReverb_VST3_validate` runs the SDK's validator (`-e`) on it.

- **Classes:** a processor (`Fx|Reverb`) and its controller, the SDK's
  recommended split.
- **Parameters:** the 23 table entries in table order. Each ParamID is a
  hash of the parameter's stable id, so automation survives any reordering
  of the table. Mode and UI Theme are list parameters, Freeze is a toggle,
  the rest are continuous and linear over their range. Changes are applied
  at block boundaries (the last point of each queue), as in the LPI build.
- **State:** ReverbCore's `LRVB` blob (`DSP/include/loudio/plugin/StateBlob.h`),
  the same bytes the LPI build saves. Not compatible with sessions saved by
  the old JUCE build (different class IDs and state format).
- **Buses:** stereo in, stereo out only; other arrangements are refused.
- **Latency:** 0 (pre-delay is an effect, not processing latency).
- **Tail:** infinite while Freeze is on; otherwise the longest the reverb
  can ring (max pre-delay plus max decay time times the max band
  multiplier, about 30 s), so hosts don't cut it off.
- **Editor:** on Windows, an `IPlugView` hosting `ReverbUi` in a cuif child
  window (HWND), DPI-aware through `IPlugViewContentScaleSupport`, edits
  sent as begin/perform/endEdit with one gesture per drag. cuif has only a
  Win32 backend, so on macOS and Linux there is no custom view and hosts
  show their generic parameter editor.

`vst3_reverb_tests` loads the built bundle the way a host does and checks
the parameter mapping, buses, state save/restore, and that the output
matches ReverbCore run directly and (on Windows, given `lpi_reverb.dll`)
the LPI Reverb, for the same input and parameters.

## No Standalone

The JUCE build also made a Standalone app. It is dropped rather than
rebuilt: a standalone needs its own audio and MIDI device layer (ASIO and
WASAPI, CoreAudio, ALSA or JACK) and a host window, which is a project in
its own right, and every VST3 host (GhostDAW included) already runs the
plugin.
