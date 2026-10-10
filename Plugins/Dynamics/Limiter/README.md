# Blueprint: Lookahead Brickwall / True-Peak Limiter

Short blueprint for the Loudio Limiter (#139), written alongside the
plugin (`VSTs/Limiter`). Sibling of the Compressor and Gate blueprints;
shares their P6 blocks and introduces P8 (`loudio::Oversampler`) on its
simplest use: true-peak detection with no processing at the higher rate.

## 1. Pipeline

```
input ── × input gain ──┬── delay (fixed) ───────────────────────────── × gain ── output
                        │                                                 ▲
                        └── [2x/4x upsample] ── |peak| ── remainder delay ─┤
                                                            per channel    │
                            required gain g = min(1, ceiling / peak)       │
                            sliding-window minimum over L+1 samples        │
                            L-point box (attack ramp)                      │
                            release one-pole, clamped from above ──────────┘
                            (stereo link mixes g with min(gL, gR))
```

## 2. The guarantee

No output sample ever exceeds the ceiling, at any block size, with any
parameter setting. It follows from the structure rather than from tuning:

```
g[n] = min(1, c / peak[n])                 required gain
m[n] = min(g[n-L .. n])                    window minimum (lookahead L)
b[n] = mean(m[n-L+1 .. n])                 box = the attack ramp
r[n] = min(b[n], aR r[n-1] + (1 - aR))     release, never above the box
y[n] = x[n-L] r[n]
```

Every `m` inside the box covers index `n−L`, so `b[n] ≤ g[n−L]`,
`r[n] ≤ b[n]`, and `|y[n]| ≤ c`. The required gain is computed against
`c × 0.99999` (−0.0001 dB) so float rounding in the average and the
final multiply cannot land a sample above `c` itself. The stereo link
mixes each channel's own `g` with `min(gL, gR)`; both are ≤ the channel's
own `g`, so any link setting keeps the guarantee.

The window minimum is a monotonic-queue sliding minimum (O(1) per
sample); the box is a running sum in double. All rings are allocated at
`prepare()` for the maximum lookahead; a lookahead change only resets
them.

## 3. True peak

With oversampling on, `peak[n]` is the largest magnitude among the 2 or
4 upsampled samples around input `n`, so inter-sample peaks are caught
(a 0 dBFS fs/4 tone sampled at 45° reads −3 dBFS by sample peaks and
0 dBFS by 4× true peak). Sample peaks are never larger than the
true-peak estimate, so the output-sample guarantee holds in every mode;
the output's true peak is within 0.1 dB of the ceiling in 4× mode. The
gain is still applied at the base rate: no downsampling filter after the
gain stage, so there is no post-limiter overshoot to worry about.

## 4. Latency

Fixed for the life of an activation, as `lpi.latency.v1` requires: the
maximum lookahead (10 ms) plus the 4× upsampler's group delay
(24 samples at any rate: 16 base samples for the first half-band stage,
8 for the second). The audio is always delayed by that; the detected
peaks are delayed by the remainder (maximum − lookahead − the current
mode's upsampler delay of 0 / 16 / 24), so the gain arrives `lookahead`
samples before the audio in every mode. 504 samples at 48 kHz.

## 5. Parameters

| id | range | default |
|---|---|---|
| `ceiling` | −30..0 dBFS | −0.3 |
| `release` | 1..1000 ms | 100 |
| `lookahead` | 0..10 ms | 2 |
| `oversampling` | Off / 2× / 4× | Off |
| `link` | 0..100 % | 100 |
| `inputGain` | −12..+24 dB | 0 |

The issue listed an output gain. A gain after the ceiling would void the
brickwall guarantee, so the Limiter has an input gain (drive into the
ceiling) instead; the ceiling itself is the output level control.

## 6. Tests (beyond the platform kit)

- **Hard guarantee**: silence → full-scale square (37-sample half
  periods, so edges hit every block phase) → silence → lone impulses,
  driven +12 dB, ceiling −1 dB, over 3 oversampling modes × 4
  lookaheads × 6 block sizes (1 to 4096, the last larger than the
  prepared maximum, so chunked): no output sample above the ceiling,
  every case within 0.2 dB of it.
- **True peak**: the 45° fs/4 tone, ceiling −6 dB: −3 dB of overs with
  sample-peak detection, at the ceiling within 0.1 dB with 4×.
- **Latency**: the upsampler's group delay measured by impulse (16 / 24);
  reported latency 504 in every mode and the audio delayed by exactly
  that; a queued lookahead/oversampling change leaves it unchanged.
- **Release**: 63% of the recovery in one release time, monotonic.
- **Link**: 100% shares the loud channel's reduction, 0% leaves the quiet
  channel untouched.
- Golden IRs: `default`, `truepeak4x`, `unlinked`.

## 7. Out of scope for v1

Processing at the oversampled rate (would need a downsampling filter
after the gain and would reintroduce overshoot), program-dependent
release, dithering, a true-peak output meter in the UI (the GR meter
shows what the detector did).
