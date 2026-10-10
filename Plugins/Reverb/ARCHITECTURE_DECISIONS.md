# Architecture Decisions: Reverb Plugin

This document records decisions made after reviewing `README.md` and `implementation_requirements.md`, where the source research needed correction or a concrete technical choice had to be made before implementation could start. Kept alongside the original blueprint docs so future work doesn't rediscover the same dead ends.

---

## 1. "Pulp Framework" does not exist

`implementation_requirements.md` names a UI toolkit called "Pulp Framework" — described as bridging React to a GPU-rendered surface via Skia + Dawn/WebGPU with a QuickJS runtime. This was checked against GitHub, the JUCE ecosystem, and general web search: **no such project exists under that name.** The closest real match for the described tech combination (WebGPU/Dawn + Skia Graphite + QuickJS) is **iPlug3**, a genuine but young/experimental C++ plugin framework — a different project entirely, and a full departure from JUCE.

A `PULP RISC-V.pdf` was subsequently added to this folder on the assumption it related to "Pulp Framework." It does not — it's the academic paper *"PULP-NN: Accelerating Quantized Neural Networks on Parallel Ultra-Low-Power RISC-V Processors"* (Garofalo, Rusci, Conti, Rossi, Benini — University of Bologna / ETH Zurich, 2019, arXiv:1908.11263). "PULP" there stands for **P**arallel **U**ltra-**L**ow-**P**ower, an open-source RISC-V microcontroller/chip research platform for embedded ML inference (e.g. the GAP-8 chip). It is unrelated to plugin UI rendering, React, Skia, WebGPU, or QuickJS.

**Decision:** treat the "Pulp Framework" section of the requirements doc as unreliable. Do not depend on it.

## 2. UI framework: custom, in C, built from scratch

Given (1), and because this plugin needs to work with **noprod** (a separate DAW prototype, see §3), the decision is to build a **custom C UI rendering framework from scratch**, rather than adopt JUCE's own `Component`/paint-based GUI system or a third-party toolkit. This is scoped as a **shared foundation** — its own project (`Framework/`), not Reverb-specific — so the other five planned plugins in this repo (EQ-3, EQ-8, Compressor, Glue, AutoTune) can build on it too instead of each reinventing GUI code.

Concretely: Win32 windowing, an OpenGL (WGL) rendering context, a small custom vector draw API, and `stb_truetype` for glyph rendering (a public-domain single-header library — pragmatic reuse, not a framework dependency). See the plan for full detail.

## 3. noprod integration: standard native-window embedding, not frame streaming

> **Superseded by §5** (Epic #97). Kept for history: noprod now hosts plugins through LPI, with the editor rendered offscreen and streamed to the browser — the frame-streaming model this section ruled out for v1.

**noprod** (`github.com/agentchieflou/noprod`, checked out locally at `C:\Users\Louar\Desktop\AntiRepo\noprod`) is a separate, early-stage AI-assisted DAW prototype: a React/DOM web frontend talking to a headless C++/JUCE backend (`apps/audio_core`, internally "Ghost DAW") over a hand-rolled WebSocket/JSON protocol. As of this writing:

- `audio_core` links **no GUI/graphics JUCE modules** (`juce_add_console_app`, no `juce_gui_basics`/`juce_opengl`) — it cannot open a window or render pixels. It scans for VST3 plugins at startup but never instantiates or processes audio through one.
- The frontend's "plugin UI" is a generic DOM parameter-slider system (`RackDevice.tsx`) driven by a plain key/value parameter bag. A placeholder entry (`"NoProd Reverb"`, params `Dry/Wet`/`Decay`) already exists in its store as a mock, not a real connection.
- A `SET_VST_PARAMETER` WebSocket message type is defined (`packages/shared/index.d.ts`) but never consumed anywhere in `main.cpp` — aspirational, unwired.
- There is no window-handle-passing, shared-memory, or frame-streaming protocol defined anywhere in noprod.

Since noprod has no native rendering surface today, the integration path is the **standard VST3 pattern**: the plugin's native window (built with `Framework/`) is created inside the host-provided parent window handle, exactly as any conventional plugin editor works in any DAW. This requires no new plumbing on noprod's side to build and test the Reverb plugin standalone in a real host (e.g. Reaper), and it composes correctly once noprod's `audio_core` is extended to actually host VST3 plugins (tracked separately — see the noprod-integration epic in the plan). No bespoke frame-streaming-into-a-`<canvas>` architecture is being built for v1.

## 4. Scope boundary: DSP/plugin format stays JUCE

> **Partly superseded by §5.** The VST3 build still uses JUCE for format plumbing, APVTS and automation, but the DSP no longer depends on JUCE: it lives in the JUCE-free `ReverbCore`, which both the VST3 wrapper and the LPI wrapper build on.

Only the **UI rendering layer** is custom C. The DSP engine and VST3 plugin format integration (`AudioProcessorValueTreeState`, audio I/O, parameter automation) remain on **JUCE (C++)**, matching every other plugin blueprint in this repo (`EQ-3`, `EQ-8`, `Compressor`, `Glue`, `AutoTune`) and avoiding a from-scratch reimplementation of VST3 hosting/format plumbing that JUCE already solves.

## 5. LPI is the primary plugin format; VST3/JUCE is the compatibility bridge

Decided in Epic #97 and built in #106 (B1), #108 (B2), #109 (B3), #111/#114 (B4).

- **Two formats, one engine.** `VSTs/Reverb/src/ReverbCore.{h,cpp}` is the JUCE-free DSP engine with its own parameter table (`kReverbParams` in `SharedStructures.h`), an RT-safe parameter queue and a versioned, id-keyed state blob (#115). The JUCE wrapper (`PluginProcessor`/`PluginEditor`) and the LPI wrapper (`lpi_reverb.cpp`) are both thin shells around it.
- **LPI** (`PluginABI/include/lpi/lpi.h`) is a plain-C ABI: one exported `lpi_get_factory`, a per-type vtable, and string-id extensions for capabilities. Parameters and state are keyed by string id, never by index.
- **Editor delivery for noprod** is the `lpi.gui.offscreen.v1` extension. cuif renders into an FBO (`cuif_window_create_offscreen`), the host pulls top-left RGBA8 frames with `read_pixels`, and it relays logical-pixel mouse events back. All GUI calls and `set_parameter_value` run on one host thread. v1 allows one open editor per process.
- **VST3 hosts (Ableton, Reaper, …)** keep the native-window editor through JUCE, as in §3.
- **Extensions beyond the editor:** `lpi.latency.v1` (delay compensation) and `lpi.params.changes.v1` (editor-originated parameter changes with gesture begin/end, pull-based) — #120, built to the noprod host's stated needs. Bypass, transport/tempo, MIDI and sidechain are deliberately not built until a host needs them.
- **Shared editor:** both editors build the same widget tree via `ReverbUi` (#119), which sends edits through a host-supplied callback.

