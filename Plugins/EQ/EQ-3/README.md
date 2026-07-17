# Architecture & Implementation Blueprint: 3-Band Frequency Isolator (EQ-3)

This document presents the engineering specification and architectural design for a zero-latency, phase-coherent **3-Band Frequency Isolator** plugin (**EQ-3**). This design is tailored for DJ-style performance tools and rapid tone carving, providing complete isolation (-infinity dB) at its low and high frequency cutoffs.

---

## 1. System Pipeline Overview

The EQ-3 splits an incoming full-bandwidth audio signal into three discrete bands (Low, Mid, High) using parallel crossover filters, scales each band independently, and sums them back together.

```
                                +-----------------------------+
                                |     Audio Input Stream      |
                                +-----------------------------+
                                               |
                     +-------------------------+-------------------------+
                     |                         |                         |
                     v                         v                         v
      +-----------------------+ +-----------------------+ +-----------------------+
      |  Low-Pass Crossover   | |   Band-Pass Filter    | |  High-Pass Crossover  |
      |   Network (LR4/LR8)   | |  (Complementary Pair) | |   Network (LR4/LR8)   |
      +-----------------------+ +-----------------------+ +-----------------------+
                     |                         |                         |
                     v                         v                         v
      +-----------------------+ +-----------------------+ +-----------------------+
      |   Low Gain Scaling    | |   Mid Gain Scaling    | |   High Gain Scaling   |
      |   (-inf to +12dB)     | |   (-inf to +12dB)     | |   (-inf to +12dB)     |
      +-----------------------+ +-----------------------+ +-----------------------+
                     |                         |                         |
                     +-------------------------+-------------------------+
                                               |
                                               v
                                +-----------------------------+
                                |      Phase Inversion /      |
                                |     Summation Matrix        |
                                +-----------------------------+
                                               |
                                               v
                                +-----------------------------+
                                |     Audio Output Stream     |
                                +-----------------------------+
```

---

## 2. Core Open-Source DSP Stack

To construct the crossover networks and gain stages with optimal execution speed, integrate the following open-source audio foundations:

*   **JUCE DSP Linkwitz-Riley Classes (`juce::dsp::LinkwitzRileyFilter`):** Provides highly optimized, out-of-the-box C++ implementations of 2nd, 4th, and 8th-order Linkwitz-Riley IIR filter structures.
*   **Faust Filter Library (`filters.lib`):** Contains formally validated equations for phase-compensated crossover splitters and DJ-style extreme isolator blocks.
*   **AudioKit Core Framework:** Offers clean, multi-platform C implementations of basic Cascaded Direct Form II biquads suitable for embedded or low-power audio systems.

---

## 3. Mathematical Foundations & Filter Design

### Step 1: Linkwitz-Riley Crossover Architecture
To achieve perfect magnitude summation across the split frequencies without phase cancellation artifacts, the EQ-3 uses **4th-Order Linkwitz-Riley (LR4)** filters. An LR4 filter is formed by cascading two identical 2nd-order Butterworth filters ($H_{B2}(s)$):

$$H_{LP\_LR4}(s) = H_{B2\_LP}^2(s) = \left( rac{\omega_c^2}{s^2 + \sqrt{2}\omega_c s + \omega_c^2} 
ight)^2$$

$$H_{HP\_LR4}(s) = H_{B2\_HP}^2(s) = \left( rac{s^2}{s^2 + \sqrt{2}\omega_c s + \omega_c^2} 
ight)^2$$

### Step 2: Deriving the Mid-Band Signal
The Mid-band is constructed by applying a low-pass filter at the upper crossover frequency ($f_{high}$) and a high-pass filter at the lower crossover frequency ($f_{low}$). To maintain phase coherence, the output signals of the Low and High sections must sum perfectly with the Mid section:

$$x(n) = y_{Low}(n) + y_{Mid}(n) + y_{High}(n)$$

Using the LR4 alignment, the outputs sum to unity magnitude, though they introduce a 180-degree phase shift at the crossover frequencies. This is corrected globally using an inverted summing matrix:

$$H_{Sum}(z) = H_{LP\_LR4}(z) + H_{BP\_LR4}(z) + H_{HP\_LR4}(z)$$

### Step 3: Gain Scaling & Total Kill Isolation
The isolated signals are scaled by linear multipliers derived from user decibel inputs ($G_{dB}$). When a band is set to its minimum value, the gain multiplier drops instantly to zero ($0.0$), ensuring a total frequency "kill":

$$g_{band} = egin{cases} 0 & 	ext{if } G_{dB} \le -60.0 \ 10^{rac{G_{dB}}{20}} & 	ext{otherwise} \end{cases}$$

$$y_{output}(n) = g_{low} \cdot y_{Low}(n) + g_{mid} \cdot y_{Mid}(n) + g_{high} \cdot y_{High}(n)$$

---

## 4. Production Implementation Checklist

| Component | Technology | Technical Purpose |
| :--- | :--- | :--- |
| **Filter Topology** | Transposed Direct Form II | Minimizes floating-point roundoff errors and internal state numerical noise. |
| **SIMD Crossover Matrix**| AVX / NEON Intrinsics | Processes Left and Right channels in parallel across all three filter paths simultaneously. |
| **Parameter Interpolation**| 1-Pole Smoothers | Smooths crossover frequency changes dynamically to prevent audible pops. |
| **Phase Alignment Block** | All-Pass Phase Delay | Matches phase responses when bypassing individual frequency bands. |

---

## References
1. Linkwitz, S. H. (1976). *Active Crossovers for Non-enclosure Loudspeaker Systems.* Journal of the Audio Engineering Society.
2. Bristow-Johnson, R. (1994). *The Equivalence of Various 2nd-Order Low-Pass Filters.* Audio Engineering Society Preprint 3810.
3. JUCE DSP Module source reference (`juce_LinkwitzRileyFilter.h`).