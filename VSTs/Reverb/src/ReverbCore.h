#pragma once

#include "loudio/dsp/DelayLine.h"
#include "loudio/dsp/AllPassDiffuser.h"
#include "loudio/dsp/EarlyReflections.h"
#include "loudio/dsp/OnePoleLPF.h"
#include "loudio/dsp/Crossover.h"
#include "loudio/dsp/EnvelopeDetector.h"
#include "loudio/dsp/SimpleBiquad.h"
#include "loudio/dsp/SimpleSmoothedValue.h"
#include "loudio/dsp/FFTAnalyzer.h"
#include "SharedStructures.h"
#include "loudio/plugin/ParamQueue.h"

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
 *     the VST3 shell (VSTs/common/vst3/LoudioVst3.h, #171) does every
 *     block, reading its own atomics (updated from the host's
 *     IParameterChanges) on the audio thread and forwarding them here on
 *     that SAME thread. No queue is needed because caller and process()
 *     are already sequential on one thread.
 *
 *   - setParameterValue(): RT-safe, enqueues via the platform's lock-free
 *     SPSC loudio::ParamQueue (DSP/include/loudio/plugin, #127), applied at
 *     the top of the next process() call. May be called from exactly one
 *     dedicated non-realtime thread over the instance's lifetime -- matches
 *     the LPI ABI's set_parameter_value contract exactly (see
 *     PluginABI/include/lpi/lpi.h), which the LPI wrapper forwards
 *     directly. The VST3 shell doesn't use it.
 *
 * Preset/state save (getState/setState) is the platform's small versioned
 * binary blob keyed on parameter id strings (loudio::StateBlob, format
 * documented in DSP/include/loudio/plugin/StateBlob.h; the Reverb's magic
 * is "LRVB"). Both the LPI and the VST3 build save exactly these bytes.
 *
 * Analyzer (FFT) data has no ring buffer of its own here: each wrapper
 * bridges it to its own UI. A host checks the dirty flag after each
 * process() call and copies out fresh magnitudes if a new frame completed.
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

    /* The platform core contract (DSP/include/loudio/plugin/CORE_CONTRACT.md). */
    static const loudio::ParamTable& table() { return kReverbParamTable; }
    int getParameterCount() const { return kNumParams; }
    bool getParameterInfo(int index, ReverbParamMeta* outMeta) const;
    float getParameterValue(int index) const;

    void setParameterValueDirect(int index, float value);
    bool setParameterValue(int index, float value);
    /* Batched setParameterValue: enqueues all `count` updates or none (if
     * any index is invalid or the queue lacks room for the whole batch), so
     * related edits -- e.g. the decay-EQ curve's three nodes -- can't be
     * half-applied. Same thread rules as setParameterValue. */
    bool setParameterValues(const ParamUpdateMessage* updates, size_t count);

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

    loudio::ParamQueue<1024> paramQueue;
};
