# Architecture & Implementation Blueprint: 8-Band Parametric Equalizer (EQ-8)

This document defines the DSP layout and mathematical specifications for a professional, high-precision **8-Band Parametric Equalizer** (**EQ-8**). This engine features flexible filter topologies per band, real-time coefficient calculation, and a low-overhead Fast Fourier Transform (**FFT**) spectrum analyzer overlay.

---

## 1. System Pipeline Overview

The EQ-8 routes an incoming audio stream through eight cascaded IIR (Infinite Impulse Response) biquad filter sections. A parallel analysis path computes real-time input/output spectral curves for GUI rendering.

```
                  +--------------------------------+
                  |       Audio Input Stream       |
                  +--------------------------------+
                                  |
            +---------------------+---------------------+
            |                                           |
            v (Signal Processing Path)                  v (Visual Analysis Path)
+-----------------------+                   +-----------------------+
|  Band 1: High-Pass /  |                   |    Hanning Windowing  |
|         Low-Shelf     |                   +-----------------------+
+-----------------------+                               |
            |                                           v
            v                               +-----------------------+
|  Band 2: Bell / Peak  |                   |   FFT Compute Block   |
+-----------------------+                   |     (1024/2048 pts)   |
            |                               +-----------------------+
            v                                           |
           ...                                          v
+-----------------------+                   +-----------------------+
|  Band 8: Low-Pass /   |                   | Logarithmic Bin Map & |
|        High-Shelf     |                   |   Temporal Smoothing  |
+-----------------------+                   +-----------------------+
            |                                           |
            +---------------------+---------------------+
                                  |
                                  v
                  +--------------------------------+
                  |  Summed Audio Output Buffer    |
                  +--------------------------------+
```

---

## 2. Core Open-Source DSP Stack

To deploy stable high-order parametric filtering alongside real-time spectrum analysis, integrate these open-source dependencies:

*   **Robert Bristow-Johnson's Audio EQ Cookbook:** The industry-standard reference for digital biquad coefficient generation.
*   **JUCE DSP IIR Framework (`juce::dsp::IIR::Filter`):** An optimized execution environment for multi-channel biquad processing that supports run-time SIMD calculations.
*   **FFTW / JUCE FFT Module:** Highly efficient Fast Fourier Transform implementations used to compute real-time spectrum curves without overloading the main audio thread.

---

## 3. Mathematical Foundations & Biquad Synthesizers

### Step 1: The Cascaded Biquad Structure
The audio stream passes through 8 sequential Direct Form II biquad structures. Each band is governed by the standard second-order linear difference equation:

$$y(n) = rac{b_0 \cdot x(n) + b_1 \cdot x(n-1) + b_2 \cdot x(n-2) - a_1 \cdot y(n-1) - a_2 \cdot y(n-2)}{a_0}$$

### Step 2: Coefficient Generation (Peaking/Bell Filter Example)
For a given sampling rate $f_s$, center frequency $f_0$, quality factor $Q$, and decibel gain $dBGain$, define intermediate variables:

$$A = 10^{rac{dBGain}{40}}, \quad \omega_0 = 2\pi rac{f_0}{f_s}, \quad lpha = rac{\sin(\omega_0)}{2Q}$$

The Robert Bristow-Johnson filter coefficients are derived as follows:

$$b_0 = 1 + lpha \cdot A, \quad b_1 = -2\cos(\omega_0), \quad b_2 = 1 - lpha \cdot A$$

$$a_0 = 1 + rac{lpha}{A}, \quad a_1 = -2\cos(\omega_0), \quad a_2 = 1 - rac{lpha}{A}$$

*   **De-Zippering Implementation:** To avoid clicking artifacts during live automation sweeps, update these coefficients using an exponential ramp function at the block level: $a_{smooth} = \lambda \cdot a_{old} + (1-\lambda) \cdot a_{new}$.

### Step 3: Real-Time Spectrum Analysis
The auxiliary visual path extracts audio frames into a circular window buffer. 
1.  Apply a Hanning window function: $w(n) = 0.5 \cdot \left(1 - \cos\left(rac{2\pi n}{N-1}
ight)
ight)$.
2.  Compute the discrete forward Fourier transform: $X(k) = \sum_{n=0}^{N-1} x(n) \cdot e^{-i 2\pi k n / N}$.
3.  Calculate the magnitude spectrum in decibels: $P(k) = 10 \cdot \log_{10}\left(|X(k)|^2
ight)$, mapping the bins logarithmically across the human audible range (20 Hz to 20 kHz).

---

## 4. Production Implementation Checklist

| Component | Technology | Technical Purpose |
| :--- | :--- | :--- |
| **Numeric Precision** | 64-bit Double Precision | Prevents quantization noise and limit cycles in low-frequency filter bands. |
| **State Typography** | Modified State Space / TDF2 | Maximizes stability during rapid modulation of frequency and Q parameters. |
| **Thread Decoupling** | Abstract FIFO Buffers | Isolates heavy FFT calculation logic from the real-time audio thread. |
| **Vectorization** | SIMD Array Intrinsics | Cascades multiple biquad calculations simultaneously for stereo frames. |

---

## References
1. Bristow-Johnson, R. (2005). *Cookbook formulae for audio EQ biquad filter coefficients.* Published Online.
2. Orfanidis, S. J. (1997). *Digital Parametric Equalizer Design with Prescribed Nyquist-Frequency Gain.* Journal of the Audio Engineering Society.
3. Zolzer, U. (2011). *Digital Audio Signal Processing.* Wiley.