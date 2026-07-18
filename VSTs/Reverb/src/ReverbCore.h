#pragma once

#include "DelayLine.h"
#include "AllPassDiffuser.h"
#include "EarlyReflections.h"
#include "OnePoleLPF.h"
#include "Crossover.h"
#include "EnvelopeDetector.h"
#include "SimpleBiquad.h"
#include "SimpleSmoothedValue.h"
#include "FFTAnalyzer.h"
#include "SharedStructures.h"
#include "cuif/ring_buffer.h"

/*
 * The Reverb's DSP engine, extracted from LoudioReverbProcessor (#100/B3) so
 * it can be hosted by something other than JUCE/APVTS -- specifically, a
 * future LPI-ABI wrapper (Epic B, #97) with no APVTS-equivalent parameter
 * store at all. Completely JUCE-free: every member is either a plain C++
 * DSP object already used standalone in DSPTests.cpp, or one of the three
 * JUCE-free replacements added alongside this file (SimpleBiquad,
 * SimpleFFT via FFTAnalyzer, SimpleSmoothedValue).
 *
 * Two ways a caller can push a parameter value in, matching two genuinely
 * different threading situations:
 *
 *   - setParameterValueDirect(): same-thread, immediate, no queue. For a
 *     host that already has its own thread-safe value source and just wants
 *     to push a fresh snapshot before each process() call -- this is what
 *     LoudioReverbProcessor::processBlock() uses every block, reading
 *     APVTS's live (host-automation-updated) values on the audio thread and
 *     forwarding them here on that SAME thread. No queue is needed because
 *     caller and process() are already sequential on one thread.
 *
 *   - setParameterValue(): RT-safe, enqueues via an internal lock-free SPSC
 *     ring buffer (same pattern as Framework/include/cuif/ring_buffer.h
 *     elsewhere in this codebase), applied at the top of the next process()
 *     call. May be called from exactly one dedicated non-realtime thread
 *     over the instance's lifetime -- matches the LPI ABI's
 *     set_parameter_value contract exactly (see
 *     PluginABI/include/lpi/lpi.h), since that's the mechanism a future LPI
 *     wrapper forwards directly. Not used by the JUCE wrapper today (its
 *     own uiToDspRingBuffer/APVTS bridge in PluginProcessor.cpp/
 *     PluginEditor.cpp is unchanged by this refactor -- that bridge is
 *     JUCE-wrapper-specific, not something ReverbCore needs to know about).
 *
 * Preset/state save (getState/setState) is a small versioned binary blob of
 * the parameter array, independent of JUCE's own APVTS XML preset format --
 * LoudioReverbProcessor::getStateInformation/setStateInformation keep using
 * APVTS's existing XML shape unchanged, preserving current preset-file
 * compatibility. This is a second, separate state mechanism for hosts that
 * have no APVTS at all.
 *
 * Analyzer (FFT) data has no ring buffer of its own here -- that's the same
 * "JUCE-wrapper-specific UI bridge" reasoning as above. A host checks the
 * dirty flag after each process() call and copies out fresh magnitudes if a
 * new frame completed.
 */
class ReverbCore {
public:
    ReverbCore();

    void prepare(double sampleRate, int maxBlockSize);

    /* In-place stereo process. Drains the RT-safe parameter-update ring
     * buffer first (see setParameterValue() above), then runs the exact
     * same signal chain as LoudioReverbProcessor::processBlock() did
     * pre-extraction: pre-delay -> diffusion -> early reflections -> FDN
     * late reverb (Householder feedback, per-line 3-band decay EQ) ->
     * post-EQ -> early/late distance balance -> stereo width -> ducking ->
     * gating -> dry/wet mix. */
    void process(float* leftChannel, float* rightChannel, int numSamples);

    int getParameterCount() const { return kNumParams; }
    bool getParameterInfo(int index, ReverbParamMeta* outMeta) const;
    float getParameterValue(int index) const;

    void setParameterValueDirect(int index, float value);
    bool setParameterValue(int index, float value);

    /* Two-call sizing idiom, matching lpi_plugin_api::get_state: buffer ==
     * nullptr returns the required size without writing anything. */
    size_t getState(void* buffer, size_t bufferSize) const;
    bool setState(const void* data, size_t size);

    bool isLeftAnalyzerFrameDirty() const { return leftAnalyzerDirty; }
    void getLeftAnalyzerMagnitudes(float* out64);
    bool isRightAnalyzerFrameDirty() const { return rightAnalyzerDirty; }
    void getRightAnalyzerMagnitudes(float* out64);

private:
    void drainPendingParameterUpdates();
    void updatePostEQ();

    double sampleRate = 44100.0;
    float params[kNumParams];

    DelayLine preDelayLeft;
    DelayLine preDelayRight;

    AllPassDiffuser diffuserLeft;
    AllPassDiffuser diffuserRight;

    EarlyReflections earlyReflectionsLeft;
    EarlyReflections earlyReflectionsRight;

    DelayLine fdnDelayLines[8];
    ThreeBandCrossover crossovers[8];
    OnePoleLPF fdnDampingFilters[8];

    SimpleBiquad postEqLeftLow, postEqLeftMid, postEqLeftHigh;
    SimpleBiquad postEqRightLow, postEqRightMid, postEqRightHigh;

    EnvelopeDetector duckDetector;
    EnvelopeDetector gateDetector;
    float gateFade = 1.0f;
    float gateTimerMs = 0.0f;

    FFTAnalyzer fftAnalyzerLeft;
    FFTAnalyzer fftAnalyzerRight;
    float leftAnalyzerMagnitudes[64] = {};
    float rightAnalyzerMagnitudes[64] = {};
    bool leftAnalyzerDirty = false;
    bool rightAnalyzerDirty = false;

    SimpleSmoothedValue smoothedPreDelayMs;
    SimpleSmoothedValue smoothedDecayTimeSec;
    SimpleSmoothedValue smoothedDamping;
    SimpleSmoothedValue smoothedWidth;
    SimpleSmoothedValue smoothedDryWet;
    SimpleSmoothedValue smoothedDistance;
    SimpleSmoothedValue smoothedThickness;

    cuif_spsc_ring_buffer paramUpdateRingBuffer;
    unsigned char paramUpdateStorage[1024];
};
