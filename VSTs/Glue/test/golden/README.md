# Golden impulse responses

Reference IRs for `testImpulseResponseMatchesGolden` in `src/DSPTests.cpp`
(DSP/include/loudio/testing/CoreTests.h). One file per variant in
`kGoldenVariants` (`ir_<name>.f32`): raw little-endian float32, 8192 left
samples then 8192 right samples, unit impulse at 44.1 kHz, every parameter
at its table default unless the variant's `configure` changes it.

`ir_default.f32` was written by tools/new_plugin.py for the scaffold's
pass-through core (an impulse in, an impulse out). After the first real
DSP change, regenerate and commit:

    dsp_tests --regen-golden

The test nulls within 1e-3 (-60 dB), not bit-exact, to absorb compiler
libm differences between the build that produced these and MSVC.
