# Blueprint: Phaser

Short blueprint for the Loudio Phaser (#144), written alongside the
plugin (`VSTs/Phaser`). Built on the P7 blocks (`AllPassStage`, `Lfo`,
`FeedbackPath`).

## 1. Pipeline

```
in ──┬──────────────────────────────────────────── dry ──┐
     │                                                   ▼
     └──► [ + ] ──► N first-order all-passes (shared corner) ──┬──► wet ──► mix ──► out
            ▲         corner = centre · 2^(±depth · 2 oct · LFO)│
            └──────────── feedback (±, tanh-clipped) ◄──────────┘
```

## 2. Stages

1. **All-pass chain**: 4, 6, 8 or 12 first-order sections
   (`y = a·x + x[n−1] − a·y[n−1]`, unity magnitude everywhere, −90° at
   the corner), all at one corner frequency. Mixed 50/50 with the dry
   signal the chain's phase produces N/2 notches at
   `fc · tan((2k+1)·π/(2N))`.
2. **Sweep**: the corner is `centre · 2^(2 · depth · LFO)`, i.e. log-spaced
   over up to ±2 octaves around the centre (depth 100 %). One sine LFO;
   the right channel reads it `stereoPhase` degrees behind.
3. **Feedback**: the chain's output back into its input, one sample
   late, −95..+95 %, tanh soft clipped so it stays bounded. Feedback
   sharpens the peaks between the notches.
4. **Mix**: 0–100 % wet; 50 % is the full notch pattern.

Corner updates are per sample (one `tan` per channel per sample; cheap
enough, and the sweep is click-free without extra smoothing). Depth and
mix ramp over 20 ms.

## 3. Parameters

| id | range | default |
|---|---|---|
| `rate` | 0.05..10 Hz | 0.4 |
| `depth` | 0..100 % (±2 octaves) | 70 |
| `centre` | 100..5000 Hz | 800 |
| `feedback` | −95..95 % | 30 |
| `stereoPhase` | 0..180 ° | 90 |
| `stages` | 4 / 6 / 8 / 12 | 6 |
| `mix` | 0..100 % | 50 |

## 4. Tests (beyond the platform kit)

- One all-pass stage has unity magnitude at 100 Hz, 1 kHz and 10 kHz
  (±0.01 dB) for a 1 kHz corner.
- Sweep off, mix 50, no feedback: 4 / 8 / 12 stages produce exactly
  2 / 4 / 6 notches on white noise (contiguous regions > 12 dB below the
  spectrum's top), at the predicted frequencies.
- Feedback 90 % with full-depth modulation on noise stays finite and
  bounded.
- Mix 0 is bit-exact dry.

## 5. Out of scope for v1

Tempo-synced rate, per-stage spread, a resonance/Q control on the
all-passes, vintage (OTA) nonlinearity.
