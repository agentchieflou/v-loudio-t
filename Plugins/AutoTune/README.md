# Architecture & Implementation Blueprint: Advanced Vocal Pitch Correction Engine

This document outlines the architectural specification and implementation strategy for a production-grade, ultra-low-latency real-time vocal pitch correction plugin (**AutoTune**). It integrates state-of-the-art open-source audio toolchains and digital signal processing (DSP) research papers.

---

## 1. System Pipeline Overview

A real-time pitch correction engine operates as a continuous, streaming block-based processor utilizing a synchronized analysis-synthesis windowing pipeline.

```
                  +--------------------------------+
                  |       Audio Input Stream       |
                  +--------------------------------+
                                  |
                                  v
                  +--------------------------------+
                  |  Overlap-Add (OLA) Windowing   |
                  +--------------------------------+
                                  |
                                  v
                  +--------------------------------+
                  |  Pitch Tracking Core (YIN/pYIN)|
                  +--------------------------------+
                                  |
            +---------------------+---------------------+
            |                                           |
            v                                           v
+-----------------------+                   +-----------------------+
| Fundamental Freq (f0) |                   |  Formant Tracking /   |
+-----------------------+                   |   Spectral Envelope   |
            |                               +-----------------------+
            v                                           |
+-----------------------+                               |
| Scale/MIDI Mapping &  |                               |
| Target Quantization   |                               |
+-----------------------+                               |
            |                                           |
            v                                           v
+-----------------------+                   +-----------------------+
|  Calculate Resampling |                   |  True Envelope / LPC  |
|      Ratio (S)        |                   |      Preservation     |
+-----------------------+                   +-----------------------+
            |                                           |
            +---------------------+---------------------+
                                  |
                                  v
                  +--------------------------------+
                  | Pitch Shifting Engine (TD-PSOLA|
                  |     or Phase Vocoder)          |
                  +--------------------------------+
                                  |
                                  v
                  +--------------------------------+
                  |      Audio Output Buffer       |
                  +--------------------------------+
```

---

## 2. Core Open-Source DSP Stack

To construct the plugin without re-engineering low-level vector mathematical frameworks or hardware abstraction layers, the following modern open-source foundations are leveraged:

### A. Core Pitch Tracking & Signal Analysis Backend
*   **PytoTune:** A performance-optimized C++20 backend equipped with Python bindings. It features a complete pipeline utilizing Google Highway SIMD and OpenMP parallelization for real-time YIN-based pitch extraction and Fourier-domain shifting.
*   **QPitch:** A fully realized JUCE-based C++ architecture supporting VST3 and CLAP formats. It offers live MIDI-mapped pitch detection and low-overhead formant preservation routines.
*   **Essentia C++ Library:** A highly optimized comprehensive library for audio analysis, containing production-tested implementations of YIN, pYIN, and Multi-Scale Autocorrelation.

### B. Time-Stretching & Pitch-Shifting Core
*   **Bungee Audio Stretch:** A phase-vocoder framework optimized for ARM NEON, x86-64 SIMD, and WebAssembly (Wasm). It delivers granular synthesis routines designed for artifact-free pitch transposition.
*   **Rubber Band Library:** The standard commercial-grade open-source engine for high-fidelity real-time time-stretching and formant-preserving pitch transposition.

### C. Deep-Learning Hybrid Pitch Tracking
*   **CREPE (ONNX Runtime):** A Convolutional Representation for Pitch Estimation. For high-accuracy scenarios, the pre-trained neural network is executed via the ONNX Runtime C++ API, enabling sub-millisecond deep-learning inference blocks.

---

## 3. Mathematical Foundations & Algorithms

### Step 1: Fundamental Frequency ($f_0$) Estimation via YIN
The system uses the YIN algorithm to compute the Cumulative Mean Normalized Difference Function ($d'_t(	au)$) over overlapping window frames. This eliminates octave-doubling errors inherent to raw autocorrelation models:

$$d_t(	au) = \sum_{j=1}^{W} (x_j - x_{j+	au})^2$$

$$d'_t(	au) = egin{cases} 1 & 	ext{if } 	au = 0 \ rac{d_t(	au)}{rac{1}{	au} \sum_{j=1}^{	au} d_t(j)} & 	ext{otherwise} \end{cases}$$

*   **Implementation Note:** To minimize algorithmic latency, implement signal decimation (downsampling by a factor of 2 or 4) prior to YIN execution, bounding the search space for $	au$ dynamically.

### Step 2: Logarithmic Pitch Quantization
Convert the continuous fundamental frequency $f_0$ into the MIDI logarithmic scale:

$$	ext{MIDI}_{real} = 69 + 12 \cdot \log_2\left(rac{f_0}{440}
ight)$$

The target note $	ext{MIDI}_{target}$ is selected based on an active scale bitmask matrix (e.g., C Major, Natural Minor). The scaling pitch ratio $S$ is computed as:

$$S = rac{f_{target}}{f_{detected}} = 2^{rac{	ext{MIDI}_{target} - 	ext{MIDI}_{real}}{12}}$$

### Step 3: Synthesis & Formant Preservation via TD-PSOLA
For ultra-low latency execution (<10ms), Time-Domain Pitch-Synchronous Overlap-Add (TD-PSOLA) is deployed:
1.  **Epoch Analysis:** Locate the glottal closure instants (Pitch-Marks) in the input block via peak-valley tracking or center-of-gravity methods.
2.  **Hanning Windowing:** Extract segments centered at each pitch mark with an analysis window length proportional to the local pitch period ($2 \cdot P_{analysis}$).
3.  **Synthesis Re-alignment:** Re-space the extracted segments onto a synthetic time grid spaced at $P_{synthesis} = P_{analysis} / S$. Overlapping segments are summed. Linear Predictive Coding (LPC) filter coefficients or True Envelope spectral mapping are overlaid to prevent vocal formant warping (the "chipmunk effect").

---

## 4. Production Implementation Checklist

| Component | Technology | Technical Purpose |
| :--- | :--- | :--- |
| **DSP Core** | C++20 Standard | Guarantees deterministic memory usage and zero-overhead abstractions. |
| **Vector Math** | Google Highway / Intel MKL | Harnesses SIMD architectures (AVX-512, NEON) for real-time Fourier operations. |
| **Plugin Framework**| JUCE Core Framework | Cross-compiles native VST3, AU, and CLAP targets across Windows, macOS, and Linux. |
| **Prototyping Engine**| Python + Librosa / PyWORLD| Facilitates mathematical verification and spectral analysis before native compilation. |

---

## References
1. de Cheveigné, A., & Kawahara, H. (2002). *YIN, a useful estimator of pitch for speech and music.* Journal of the Acoustical Society of America.
2. Laroche, J., & Dolson, M. (1999). *New phase-vocoder techniques for real-time pitch shifting.* IEEE Transactions on Speech and Audio Processing.
3. PytoTune C++ Core Engine open-source repository (GitHub).
4. QPitch Engine JUCE-Plugin open-source repository (GitHub).