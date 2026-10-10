# Blueprint: Noise Gate / Downward Expander

Short blueprint for the Loudio Gate (#138), written alongside the plugin
(`VSTs/Gate`). Sibling of the Compressor blueprint
(`Plugins/Compressors/Compressor/README.md`); shares its P6 blocks.

## 1. Pipeline

```
input ──┬── Lookahead delay ─────────────────────────────┬── × gain ── output
        │                                                │
        └── sidechain HPF ── peak detector ── hysteresis ── hold ── attack/release ── gain
             (optional)       (fast)          open/close   timer    (linear one-pole)
```

Everything on the sidechain is stereo-linked (max of |L|, |R|): the
gate opens and closes both channels together.

## 2. Stages

1. **Sidechain HPF** (`loudio::SidechainHpf`): 2nd-order Butterworth
   high-pass, 0 Hz = off, so rumble and bleed below the instrument's
   range do not hold the gate open.
2. **Detector** (`loudio::LevelDetector`, Peak): near-instant attack,
   5 ms release. Fast enough that closing is not delayed noticeably,
   slow enough that the half-cycle dips of anything above ~200 Hz stay
   within the hysteresis; below that the **hold** keeps the gate open
   across the dips (that is what hold is for on every hardware gate).
3. **Hysteresis** (`loudio::Hysteresis`): open when the level in dB
   rises above `threshold`, close only once it falls below
   `threshold − hysteresis`. A level hovering around the threshold
   cannot chatter.
4. **Hold**: once the hysteresis says "close", the gate stays open for
   `hold` ms; a re-open during the hold restarts it.
5. **Attack / release**: the gain is a linear one-pole toward 1 (open)
   with the attack time constant or toward the floor (closed) with the
   release time constant, so a step reaches 63% of the way in one
   attack / release time. A linear ramp is an exponential fade in
   amplitude, i.e. a constant dB/s slope: the classic gate tail.
6. **Range**: the closed floor in dB. −80 dB is effectively silence;
   −20 dB leaves exactly −20 dB of the signal and makes the gate a
   downward expander with a fixed depth.
7. **Lookahead** (`loudio::Lookahead`): the audio path is delayed by
   `lookahead` ms so the gate is already opening when the transient
   arrives; reported as the plugin's latency (`lpi.latency.v1` /
   `setLatencySamples`).

## 3. Parameters

| id | range | default |
|---|---|---|
| `threshold` | −80..0 dB | −40 |
| `hysteresis` | 0..24 dB | 6 |
| `attack` | 0.01..100 ms | 1 |
| `hold` | 0..1000 ms | 50 |
| `release` | 1..2000 ms | 100 |
| `range` | −80..0 dB | −80 |
| `scHpf` | 0..500 Hz | 0 |
| `lookahead` | 0..10 ms | 0 |

## 4. Tests (beyond the platform kit)

- A −20 dBFS burst into silence: the gain reaches 63% within one attack
  time of the burst, stays fully open through the hold, and has fallen
  to 37% one release time after the hold ends (plus the detector's
  decay to the close threshold).
- A level toggling ±1 dB around the threshold every 20 ms produces no
  state change at all with 6 dB of hysteresis (and chatters with 0).
- Range −20 dB: a steady signal below the threshold comes out at
  exactly −20 dB relative to the input once settled.
- `latencySamples()` == lookahead samples; the audio is delayed by that.

## 5. Out of scope for v1

External sidechain (LPI v1 has no sidechain bus), ratio-style expansion
(the Compressor's `GainComputer` expander mode is available when a
ratio control is wanted), ducking mode.
