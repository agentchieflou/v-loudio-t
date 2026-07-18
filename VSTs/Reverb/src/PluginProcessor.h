#pragma once

#include <JuceHeader.h>
#include "ReverbCore.h"
#include "cuif/ring_buffer.h"
#include "SharedStructures.h"

/*
 * Thin JUCE/VST3-facing wrapper (#100/B3) around ReverbCore, the actual
 * JUCE-free DSP engine. This class's job is exactly two bridges:
 *
 *   1. APVTS <-> ReverbCore parameter sync, each block: drain UI-driven
 *      ring-buffer messages into APVTS (unchanged from before extraction,
 *      keeps the host's own automation-lane/generic-UI view of every
 *      parameter correct), then push every current APVTS value into
 *      ReverbCore via setParameterValueDirect() (same-thread, no queue --
 *      see ReverbCore.h's header comment for why this is safe).
 *   2. ReverbCore's analyzer-magnitude output -> dspToUiRingBuffer, exactly
 *      like before extraction, so PluginEditor.cpp's reading side is
 *      completely unchanged.
 *
 * getStateInformation/setStateInformation still serialize APVTS's own XML
 * state exactly as before -- this preserves existing .vstpreset/DAW-session
 * compatibility. ReverbCore has its own separate, JUCE-free getState/
 * setState (a small binary blob) for a future host-agnostic (LPI) wrapper
 * with no APVTS-equivalent preset mechanism at all; this class doesn't use
 * it.
 */
class LoudioReverbProcessor : public juce::AudioProcessor {
public:
    LoudioReverbProcessor();
    ~LoudioReverbProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    /* Shared SPSC lock-free ring buffers for DSP <-> UI communications --
     * unchanged in shape and ownership from before extraction; only the
     * DSP math that fills/drains them moved into ReverbCore. */
    cuif_spsc_ring_buffer dspToUiRingBuffer;
    cuif_spsc_ring_buffer uiToDspRingBuffer;

private:
    ReverbCore core;

    /* Pre-allocated storage for ring buffers (1024 bytes is ample for FIFO data) */
    unsigned char dspToUiStorage[1024];
    unsigned char uiToDspStorage[1024];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoudioReverbProcessor)
};
