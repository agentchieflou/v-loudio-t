# Architecture & Implementation Blueprint: Digital Dynamic Range Compressor

This document establishes the DSP architecture and structural specifications for a production-ready, ultra-low distortion **Digital Dynamic Range Compressor**. The engine targets predictable transient control with low CPU overhead.

---

## 1. System Pipeline Overview

A standard feed-forward dynamic range compressor evaluates the input signal amplitude, processes it via a non-linear gain computer, smoothes the resulting gain envelope, and applies precise attenuation.

```
                  +--------------------------------+
                  |       Audio Input Stream       |
                  +--------------------------------+
                                  |
            +---------------------+---------------------+
            |                                           |
            v (Main Audio Path)                         v (Sidechain Path)
+-----------------------+                   +-----------------------+
|  Delay Line /         |                   |   Absolute Value /    |
|  Lookahead Buffer     |                   |    RMS Estimation     |
+-----------------------+                   +-----------------------+
            |                                           |
            |                                           v
            |                               +-----------------------+
            |                               |   Logarithmic dB      |
            |                               |     Conversion    |
            |                               +-----------------------+
            |                                           |
            |                                           v
            |                               +-----------------------+
            |                               |     Gain Computer     |
            |                               |  (Threshold/Ratio/Knee|
            |                               +-----------------------+
            |                                           |
            |                                           v
            |                               +-----------------------+
            |                               |   Envelope Follower   |
            |                               |   (Attack / Release)  |
            |                               +-----------------------+
            |                                           |
            |                                           v
            |                               +-----------------------+
            |                               | Linear Gain Reduction |
            |                               |   Coefficient Calc    |
            |                               +-----------------------+
            |                                           |
            +---------------------+---------------------+
                                  |
                                  v
                  +--------------------------------+
                  | Multiplier / Gain Attenuation  |
                  +--------------------------------+
                                  |
                                  v
                  +--------------------------------+
                  |  Makeup Gain & Soft Clipping   |
                  +--------------------------------+
                                  |
                                  v
                  +--------------------------------+
                  |         Audio Output           |
                  +--------------------------------+
```

---

## 2. Core Open-Source DSP Stack

To construct this dynamic processor without writing basic filters and matrix calculations from scratch, utilize the following open-source resources:

*   **JUCE DSP Module (`juce::dsp::Compressor`):** Provides a robust baseline implementation of a feed-forward compressor engine, complete with multi-channel layout configurations and optimized SIMD processing blocks.
*   **Faust DSP Libraries (`compressor.lib`):** The functional programming audio language Faust contains highly optimized, formally verified mathematical topologies for peak and RMS envelope followers, auto-attack/release matrices, and soft-knee configurations.
*   **AudioTK (ATK):** A modern C++ audio processing library specifically optimized for pipeline-based dynamics execution. It provides distinct decoupled structures for sidechain processing, envelope followers, and gain computers.

---

## 3. Mathematical Foundations & Algorithms

### Step 1: Sidechain Power Detection
The input signal $x(n)$ is mapped to its signal envelope in the decibel domain. Detection can be Peak-based or Root-Mean-Square (RMS) based:

$$x_{peak}(n) = |x(n)|$$

$$x_{RMS}(n) = \sqrt{rac{1}{W} \sum_{i=0}^{W-1} x(n-i)^2}$$

Convert the detected level into the logarithmic decibel domain to linearize human perception of loudness:

$$X_{dB}(n) = 20 \cdot \log_{10}(x_{detect}(n))$$

### Step 2: The Gain Computer (Soft-Knee Topology)
The gain computer determines the raw gain reduction $G_c(n)$ needed based on the Threshold ($T$), Ratio ($R$), and Knee Width ($W_k$). A soft-knee configuration interpolates smoothly around the threshold to prevent discontinuity artifacts:

$$G_c(n) = egin{cases} X_{dB}(n) & 	ext{if } 2(X_{dB}(n) - T) < -W_k \ X_{dB}(n) + rac{(rac{1}{R} - 1)(X_{dB}(n) - T + rac{W_k}{2})^2}{2W_k} & 	ext{if } 2|X_{dB}(n) - T| \le W_k \ T + rac{X_{dB}(n) - T}{R} & 	ext{if } 2(X_{dB}(n) - T) > W_k \end{cases}$$

The dynamic raw attenuation requirement is:

$$\Delta G_{dB}(n) = G_c(n) - X_{dB}(n)$$

### Step 3: Envelope Smoothing Filter
To smooth the raw attenuation and match target Attack ($	au_{att}$) and Release ($	au_{rel}$) time constants, apply a decoupled, non-linear one-pole smoothing filter:

$$lpha_{att} = 1 - e^{-rac{1}{f_s \cdot 	au_{att}}}, \quad lpha_{rel} = 1 - e^{-rac{1}{f_s \cdot 	au_{rel}}}$$

$$G_s(n) = egin{cases} lpha_{att} \cdot \Delta G_{dB}(n) + (1 - lpha_{att}) \cdot G_s(n-1) & 	ext{if } \Delta G_{dB}(n) < G_s(n-1) \ lpha_{rel} \cdot \Delta G_{dB}(n) + (1 - lpha_{rel}) \cdot G_s(n-1) & 	ext{if } \Delta G_{dB}(n) \ge G_s(n-1) \end{cases}$$

Convert the smoothed decibel attenuation back to a linear gain coefficient:

$$g_{linear}(n) = 10^{rac{G_s(n)}{20}}$$

Apply the final attenuation and makeup gain ($M_{dB}$):

$$y(n) = x(n - D) \cdot g_{linear}(n) \cdot 10^{rac{M_{dB}}{20}}$$

Where $D$ represents the lookahead delay buffer length.

---

## 4. Production Implementation Checklist

| Component | Technology | Technical Purpose |
| :--- | :--- | :--- |
| **Lookahead Engine** | Circular Buffer Matrix | Implements low-overhead sample delay lines for transparent transient capture. |
| **Oversampling Block**| `juce::dsp::Oversampling` | Deploys 2x/4x polyphase FIR oversampling to prevent aliasing generated by rapid gain modulation. |
| **SIMD Gain Vector** | Intel AVX2 / ARM NEON | Processes multichannel audio frames simultaneously in vectorized blocks. |
| **Parameter Smoothing**| Linear/Exponential Ramps | Prevents "zipper noise" during manual GUI modifications of threshold or makeup parameters. |

---

## References
1. Giannoulis, D., Massberg, M., & Reiss, J. D. (2012). *Digital Dynamic Range Compressor Design—A Tutorial and Analysis.* Journal of the Audio Engineering Society.
2. Massberg, M. (2009). *Digital Dynamic Range Compressor Design.* Audio Engineering Society Convention 127.
3. Faust Open Source Libraries: `compressor.lib` documentation.