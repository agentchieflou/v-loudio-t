# v-loudio-t
Virtual Studio Technologies, created by Lou, so they're Virtual Loudio Technoligies. 

VST collector

## Formats

Every plugin here builds two ways from one DSP core (its `<Name>Core`):

- **VST3**, on the Steinberg VST3 SDK directly (MIT, `ThirdParty/vst3sdk`;
  no JUCE, #171), through the shared shell in `common/vst3`. It builds on
  Windows, macOS and Linux and passes the SDK's validator in CI. The cuif
  editor is Windows-only for now (cuif has a Win32 backend only); elsewhere
  hosts show their generic parameter editor.
- **LPI** (`PluginABI`), noprod's own plugin interface, with the cuif UI
  rendered offscreen.

There is no Standalone app; see `Reverb/README.md` for why.
