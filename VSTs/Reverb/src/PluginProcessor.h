#pragma once

#include <JuceHeader.h>
#include "DelayLine.h"
#include "AllPassDiffuser.h"
#include "EarlyReflections.h"
#include "OnePoleLPF.h"

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

private:
    double currentSampleRate = 44100.0;

    DelayLine preDelayLeft;
    DelayLine preDelayRight;

    AllPassDiffuser diffuserLeft;
    AllPassDiffuser diffuserRight;

    EarlyReflections earlyReflectionsLeft;
    EarlyReflections earlyReflectionsRight;

    DelayLine fdnDelayLines[8];
    OnePoleLPF fdnDampingFilters[8];

    juce::LinearSmoothedValue<float> smoothedPreDelayMs;
    juce::LinearSmoothedValue<float> smoothedDecayTimeSec;
    juce::LinearSmoothedValue<float> smoothedDamping;
    juce::LinearSmoothedValue<float> smoothedWidth;
    juce::LinearSmoothedValue<float> smoothedDryWet;
    juce::LinearSmoothedValue<float> smoothedDistance;
    juce::LinearSmoothedValue<float> smoothedThickness;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoudioReverbProcessor)
};
