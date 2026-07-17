# Architecture & Implementation Blueprint: Hybrid Feedback Delay Network Reverb

This document details the architectural layout and academic principles required to build a production-grade, highly dense **Hybrid Feedback Delay Network (FDN) Reverb** plugin. This design combines multi-tap early reflection delay lines with a stable, high-order late reverberation FDN engine.

---

## 1. System Pipeline Overview

The reverberation processor splits the input signal into two main processing paths: an early reflection matrix that simulates initial boundary walls, and a multi-channel FDN that generates a dense, exponentially decaying late reverberant tail.

```
                                +-----------------------------+
                                |     Audio Input Stream      |
                                +-----------------------------+
                                               |
                     +-------------------------+-------------------------+
                     |                                                   |
                     v (Early Reflections)                               v (Late Reverberation)
      +-----------------------+                           +-----------------------+
      |  Tapped Delay Lines   |                           | Pre-Delay Buffer Line |
      |  (Geometric Arrays)   |                           +-----------------------+
      +-----------------------+                                          |
                     |                                                   v
                     v                                    +-----------------------+
      +-----------------------+                           |  All-Pass Diffusers   |
      |   Stereo Cross-Mix    |                           |  (Transient Smearing) |
      +-----------------------+                           +-----------------------+
                     |                                                   |
                     |                                                   v
                     |                                    +-----------------------+
                     |                    +-------------->| FDN Delay Line Array  |
                     |                    |               | (Prime Sample Lengths)|
                     |                    |               +-----------------------+
                     |                    |                              |
                     |                    |                              v
                     |                    |               +-----------------------+
                     |                    |               | Absorptive Low-Pass   |
                     |                    |               | Filters (Damping)     |
                     |                    |               +-----------------------+
                     |                    |                              |
                     |                    |                              v
                     |                    |               +-----------------------+
                     |                    |               |   Unitary Feedback    |
                     |                    |               |    Matrix (Householder|
                     |                    |               +-----------------------+
                     |                    |                              |
                     |                    +---------( Feedback Loop )----+
                     |                                                   |
                     |                                                   v
                     +-------------------------+-------------------------+
                                               |
                                               v
                                +-----------------------------+
                                |  Global Dry/Wet Matrix &    |
                                |     Stereo Width Nodes      |
                                +-----------------------------+
                                               |
                                               v
                                +-----------------------------+
                                |     Audio Output Stream     |
                                +-----------------------------+
```

---

## 2. Core Open-Source DSP Stack

To build dense reverberant environments without manually writing multi-dimensional matrix operations or basic delay lines, leverage these open-source tools:

*   **Faust Reverb Library (`reverbs.lib`):** Contains validated reference layouts for production reverbs like `zita_rev1` and classic FDN architectures.
*   **JUCE Reverb Core Module (`juce::dsp::Reverb`):** Provides an optimized baseline implementation of a Schroeder-style reverberator, useful for low-overhead mobile applications.
*   **Eigen C++ Linear Algebra Library:** An ultra-fast, header-only template library for matrix operations. It is ideal for calculating real-time Householder or Hadamard transformation matrices in the late reverb loop.

---

## 3. Mathematical Foundations & Structural Design

### Step 1: Early Reflection Tapped Delays
Early reflections are generated using a series of asymmetrical tapped delay lines. The tap times are chosen based on prime numbers or geometric expansion to avoid resonant modal clusters:

$$y_{early}(n) = \sum_{i=1}^{M} g_i \cdot x(n - d_i)$$

Where $g_i$ represents the absorption coefficient of the virtual boundary surface and $d_i$ represents the sample delay length.

### Step 2: Late Reverberation Feedback Delay Network (FDN)
The late reverberation engine uses an $N 	imes N$ Feedback Delay Network. The core state-space update is defined by:

$$\mathbf{s}(n) = \mathbf{V} \cdot \mathbf{y}_{fdn}(n) + \mathbf{x}_{in}(n)$$

$$\mathbf{y}_{fdn}(n) = egin{bmatrix} s_1(n - m_1) \ s_2(n - m_2) \ dots \ s_N(n - m_N) \end{bmatrix}$$

To ensure loop stability and prevent sudden volume explosions, the feedback matrix $\mathbf{A}$ must be **unitary** or an orthogonal **Householder Matrix**:

$$\mathbf{A} = \mathbf{I} - rac{2}{\mathbf{v}^T\mathbf{v}} \mathbf{v}\mathbf{v}^T$$

Because $\mathbf{A}^T\mathbf{A} = \mathbf{I}$, energy is conserved perfectly within the feedback network, ensuring a smooth, predictable decay curve.

### Step 3: High-Frequency Room Damping
To simulate natural air and surface absorption, insert a one-pole low-pass filter $H_i(z)$ after each delay line within the loop:

$$H_i(z) = rac{1 - g_{hf}}{1 - g_{hf} \cdot z^{-1}}$$

The damping coefficient $g_{hf}$ scales dynamically based on the target high-frequency reverberation time ($T_{60}(f)$).

---

## 4. Production Implementation Checklist

| Component | Technology | Technical Purpose |
| :--- | :--- | :--- |
| **Delay Lengths** | Prime Numbers Matrix | Prevents metallic ringing by choosing mutually prime delay sample lengths. |
| **Matrix Calculations**| Vectorized Householder | Uses SIMD operations to compute feedback matrices without stalling the audio thread. |
| **Transient Smoothing**| Cascaded All-Pass Chains | Diffuses sharp incoming transients quickly to build dense late reflections. |
| **Memory Allocation** | Pre-allocated Ring Buffers| Avoids real-time memory allocations to ensure audio thread safety. |

---

## References
1. Schroeder, M. R. (1962). *Natural-Sounding Artificial Reverberation.* Journal of the Audio Engineering Society.
2. Jot, J. M., & Chaigne, A. (1991). *Digital Delay Networks for Designing Artificial Reverberators.* Audio Engineering Society Convention 90.
3. Rocchesso, D., & Smith, J. O. (1997). *Circulant and Elliptic Matrices for Synthesis of Low-Modal-Density Reverbs.* IEEE Transactions on Speech and Audio Processing.