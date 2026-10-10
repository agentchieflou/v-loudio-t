# Golden impulse responses (#44)

Reference IRs for `testReverbCoreImpulseResponseMatchesGolden` in
`src/DSPTests.cpp`. One file per reverb mode (`ir_mode0.f32` Room ...
`ir_mode4.f32` Spring): raw little-endian float32, 8192 left samples then
8192 right samples, unit impulse at 44.1 kHz, dryWet = 1.0, every other
parameter at its `kReverbParams` default.

After an intentional DSP change, regenerate and commit the new files:

    dsp_tests --regen-golden

The test nulls within 1e-3 (-60 dB), not bit-exact, to absorb compiler
libm differences between the g++ build that produced these and MSVC.
