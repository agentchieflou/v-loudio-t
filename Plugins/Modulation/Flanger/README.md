# Blueprint: Flanger

Short blueprint for the Loudio Flanger (#143), written alongside the
plugin (`VSTs/Flanger`). Built on the P7 modulation blocks
(`ModulatedDelay`, `Lfo`, `FeedbackPath`).

## 1. Pipeline

```
in ──┬──────────────────────────── dry (or dry delayed by `centre` when through-zero) ──┐
     │                                                                                 ▼
     └──► [ + ] ──► short delay: centre × (1 + depth · LFO) ──┬──► wet ──────────► mix ──► out
            ▲                                                 │
            └──────── feedback (±, tanh-clipped) ◄────────────┘
```

One line per channel; the right channel's LFO runs `stereoPhase` degrees
behind the left (`Lfo::valueAt(phase + offset)`), so the two combs sweep
offset in time.

## 2. Stages

1. **Delay**: `ModulatedDelay`, centre 0.1–10 ms, modulated by a sine LFO
   (0.05–10 Hz) with `depth` as a fraction of the centre (0–100 %), so
   the delay sweeps `centre × (1 ± depth)` and never goes negative. The
   line's own 20 ms ramp keeps centre changes click-free.
2. **Feedback**: `FeedbackPath` from the wet output back to the line's
   input, −95..+95 %; negative feedback flips the comb's peaks and
   notches. The tanh soft clip keeps 95 % bounded.
3. **Through-zero**: the dry path is delayed by the centre time (a second
   `ModulatedDelay` with no modulation), so the modulated path crosses it
   and the flange goes "through zero" (the classic tape effect where the
   two paths swap order). This delays the whole output by the centre time
   while on; it is part of the effect, not reported latency, since the
   wet/dry relationship is what matters.
4. **Mix**: 0–100 % wet; 50 % is the full comb.

## 3. Parameters

| id | range | default |
|---|---|---|
| `rate` | 0.05..10 Hz | 0.3 |
| `depth` | 0..100 % | 50 |
| `centre` | 0.1..10 ms | 3 |
| `feedback` | −95..95 % | 30 |
| `stereoPhase` | 0..180 ° | 90 |
| `throughZero` | off / on | off |
| `mix` | 0..100 % | 50 |

## 4. Tests (beyond the platform kit)

- Modulation off, centre 128 samples (2.667 ms), mix 50, no feedback:
  the comb's notches sit at odd multiples of fs/256 = 187.5 Hz (bins 16,
  48, 80 of a 4096-point FFT on white noise, ≥ 20 dB below the peaks at
  bins 32, 64).
- Feedback 90 % on noise stays finite and bounded.
- Stereo phase 180°: the right channel's delay trajectory is the left's
  half an LFO period later (measured on an impulse train).
- Through-zero with depth 0: output == input delayed by the centre.
- Mix 0 is bit-exact dry (through-zero off).

## 5. Out of scope for v1

Tempo-synced rate, LFO shapes other than sine, a high-pass in the
feedback path, a stereo-width control.
