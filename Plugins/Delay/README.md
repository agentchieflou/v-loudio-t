# Blueprint: Stereo / Ping-Pong Delay

Short blueprint for the Loudio Delay (#142), written alongside the plugin
(`VSTs/Delay`). Built on the P7 modulation blocks (`ModulatedDelay`,
`Lfo`, `FeedbackPath`) and the P9 transport (`loudio::TransportInfo`).

## 1. Pipeline

```
in L ──┬──────────────────────────────────────────────────────── dry ──┐
       │   ┌─────────────────────────── feedback (filtered) ◄───┐      │
       ▼   ▼                                                   │      ▼
   [ + ] ── delay L (time L, ± modulation) ── low cut ── high cut ──┬──► wet L ──► mix ── out L
                                                                   │
                      cross-feed ◄──────────────────────────────────┤
                                                                   │
   [ + ] ── delay R (time R, ± modulation) ── low cut ── high cut ──┴──► wet R ──► mix ── out R
       ▲   ▲                                                   │
       │   └─────────────────────────── feedback (filtered) ◄───┘
in R ──┴──────────────────────────────────────────────────────── dry ──┘
```

Two independent delay lines. Each line's output goes through the
feedback filters (12 dB/oct low cut and high cut), then back into its
own input scaled by `feedback`, and into the *other* line's input scaled
by `feedback × crossFeed`: cross-feed 0 is a dual-mono delay, 100 is a
full ping-pong (each repeat hops sides). The feedback path is soft
clipped (`FeedbackPath`, tanh) so settings at or above unity feedback
cannot run away. Note the filters sit *inside* the loop, so every repeat
is darker than the last, the classic tape/analogue behaviour.

## 2. Time

Each side has a free time in ms or, with sync on, a tempo division from
the host transport (`lpi.transport.v1` / JUCE playhead): 1/32 … 1/1 with
dotted and triplet variants (`divisions[] × 60 / bpm`). Without a valid
tempo the last known one (default 120 BPM) is used, so synced delays keep
working in hosts with no transport. Time changes glide (20 ms linear
ramp on the delay in samples, `ModulatedDelay`), which gives the tape-like
pitch slide and never a click or dropout. Fractional times are read with
`DelayLine`'s exact integer/fraction split (#112).

## 3. Modulation

One `Lfo` (sine), rate 0.05–10 Hz, depth 0–10 ms, applied to both lines
with the right side a quarter cycle behind (`valueAt(phase + 0.25)`), for
subtle chorus-like movement in the repeats. Depth 0 leaves the lines
exactly static.

## 4. Parameters

| id | range | default |
|---|---|---|
| `timeL`, `timeR` | 1..2000 ms | 375, 375 |
| `sync` | off / on | off |
| `divisionL`, `divisionR` | 1/32 … 1/1 (+ dotted, triplet) | 1/8 dotted, 1/8 |
| `feedback` | 0..110 % | 40 |
| `crossFeed` | 0..100 % | 0 |
| `lowCut` | 20..2000 Hz | 100 |
| `highCut` | 1000..20000 Hz | 8000 |
| `modRate` | 0.05..10 Hz | 0.5 |
| `modDepth` | 0..10 ms | 0 |
| `mix` | 0..100 % | 35 |

## 5. Tests (beyond the platform kit)

- An impulse with 500 ms (24 000 samples) time, feedback 50 %, no
  filters, no modulation produces echoes at exactly 24 000, 48 000,
  72 000 samples, each exactly −6.02 dB below the last; a fractional
  time (20.5 ms) is interpolated with no dropped samples.
- Cross-feed 100: a left-only impulse comes back on the right first,
  then the left, and so on.
- Sync on at 120 BPM: 1/4 = 500 ms exactly, 1/8 dotted = 375 ms, 1/8
  triplet = 166.67 ms; a tempo change moves the repeats.
- A time jump (100 → 400 ms) on a sine produces no sample step larger
  than the glide allows (bounded, no click) and settles at the new time.
- Mix 0 is bit-exact dry (no latency to compensate).

## 6. Out of scope for v1

Tape saturation and wow/flutter modelling, multi-tap, diffusion, a
tempo-synced modulation rate, an external sidechain duck.
