#pragma once

#include <JuceHeader.h>
#include "DelayLine.h"
#include "AllPassDiffuser.h"
#include "EarlyReflections.h"
#include "OnePoleLPF.h"
#include "Crossover.h"
#include "EnvelopeDetector.h"
#include "FFTAnalyzer.h"
#include "cuif/ring_buffer.h"
#include "SharedStructures.h"
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

    /* Shared SPSC lock-free ring buffers for DSP <-> UI communications */
    cuif_spsc_ring_buffer dspToUiRingBuffer;
    cuif_spsc_ring_buffer uiToDspRingBuffer;

private:
    double currentSampleRate = 44100.0;

    DelayLine preDelayLeft;
    DelayLine preDelayRight;

    AllPassDiffuser diffuserLeft;
    AllPassDiffuser diffuserRight;

    EarlyReflections earlyReflectionsLeft;
    EarlyReflections earlyReflectionsRight;

    DelayLine fdnDelayLines[8];
    ThreeBandCrossover crossovers[8];
    OnePoleLPF fdnDampingFilters[8];

    using PostEQChain = juce::dsp::ProcessorChain<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Filter<float>>;
    PostEQChain postEqLeft;
    PostEQChain postEqRight;

    EnvelopeDetector duckDetector;
    EnvelopeDetector gateDetector;
    
    float gateFade = 1.0f;
    float gateTimerMs = 0.0f;

    void updatePostEQ();

    FFTAnalyzer fftAnalyzerLeft;
    FFTAnalyzer fftAnalyzerRight;

    /* Pre-allocated storage for ring buffers (1024 bytes is ample for FIFO data) */
    unsigned char dspToUiStorage[1024];
    unsigned char uiToDspStorage[1024];

    juce::LinearSmoothedValue<float> smoothedPreDelayMs;
    juce::LinearSmoothedValue<float> smoothedDecayTimeSec;
    juce::LinearSmoothedValue<float> smoothedDamping;
    juce::LinearSmoothedValue<float> smoothedWidth;
    juce::LinearSmoothedValue<float> smoothedDryWet;
    juce::LinearSmoothedValue<float> smoothedDistance;
    juce::LinearSmoothedValue<float> smoothedThickness;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoudioReverbProcessor)
};
