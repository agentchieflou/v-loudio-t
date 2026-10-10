# Plugin core contract

A plugin's DSP engine (its *core*: `ReverbCore` today, `CompressorCore`
etc. tomorrow) is a plain C++ class with no base class. The format
wrappers (the LPI wrapper template `PluginABI/wrapper/lpi/plugin_template.h`,
P3's VST3 common code) template on it,
so a core only has to provide the members below with these exact meanings.
Everything parameter-shaped comes from the three headers next to this file:

| Header | Provides |
|---|---|
| `ParamTable.h` | `ParamMeta`, `ParamUpdate`, `ParamFlags` (== `LPI_PARAM_*`), `ParamTable` (`findById`, `sanitize`, `toWidget`/`fromWidget`, `fillDefaults`) |
| `StateBlob.h` | `StateBlob::write`/`read`: the id-keyed v1 blob, two-call sizing, validate-then-apply, optional legacy raw-array load |
| `ParamQueue.h` | `ParamQueue<CapacityBytes>`: SPSC, allocation-free, all-or-nothing `push`/`pushAll`, `drainInto` on the audio thread |
| `Transport.h` | `TransportInfo` + `TransportFlags` (== `LPI_TRANSPORT_*`), tempo division helpers |

## Required members

```cpp
class XxxCore {
public:
    XxxCore();                                   // every parameter at its default; no allocation needed before prepare()
    void prepare(double sampleRate, int maxBlockSize);   // may allocate; called before the first process()
    void process(float* left, float* right, int numSamples);  // in-place stereo; drains the queue first; never allocates

    static const loudio::ParamTable& table();    // the plugin's one parameter table (entries[i].index == i)
    int   getParameterCount() const;             // == table().count
    bool  getParameterInfo(int index, loudio::ParamMeta* out) const;  // false on a bad index
    float getParameterValue(int index) const;    // 0.0f on a bad index

    void  setParameterValueDirect(int index, float value);              // same thread as process(); immediate; bad index = no-op
    bool  setParameterValue(int index, float value);                    // one non-RT producer thread; queued; false on bad index / full
    bool  setParameterValues(const loudio::ParamUpdate* u, size_t n);   // same, all-or-nothing

    size_t getState(void* buffer, size_t size) const;   // StateBlob::write under the plugin's own 4-byte magic
    bool   setState(const void* data, size_t size);     // StateBlob::read; false leaves state untouched
};
```

## Optional members (wrappers detect them, nothing is required)

- `int latencySamples() const` – reported through `lpi.latency.v1` / `setLatencySamples`. Absent means 0.
- Analyzer / meter hooks, e.g. `bool isLeftAnalyzerFrameDirty() const; void getLeftAnalyzerMagnitudes(float* out64);`. Reading clears the dirty flag. Torn reads are acceptable for cosmetic displays.
- `void setTransport(const loudio::TransportInfo&)` (`Transport.h`, mirrors `lpi.transport.v1`, #135): both wrappers detect it and call it on the audio thread once per block before `process()`. The host may pass `PLAYING` clear with a stale position, or never call it: keep a free-running fallback and resync only when `PLAYING` and `PPQ_VALID` are both set. The LPI wrapper advertises `lpi.transport.v1` only when this member exists. `tempoDivisionHz` / `tempoDivisionSeconds` / `tempoPhaseAtPpq` turn tempo and position into LFO rates, delay times and phases.

## Threading

- `process()` and `setParameterValueDirect()` run on the audio thread.
- `setParameterValue(s)` runs on exactly one non-realtime thread for the
  instance's lifetime (the LPI ABI's `set_parameter_value` contract). The
  JUCE wrapper does not use it: it reads APVTS on the audio thread and calls
  `setParameterValueDirect()`.
- `getState()`/`setState()` run on the host's main thread while the plugin
  is deactivated, or on the audio thread. They touch only the value array.

## Testing a core

Each plugin keeps its own test executable (the Reverb's `dsp_tests`). The
platform asks for, at minimum: `table().indicesAreConsistent()`, defaults
match the table, direct set applies immediately, queued set applies after
`process()`, all-or-nothing batch, state round-trip, zero heap allocations
inside `process()`, and a golden impulse-response null test.
