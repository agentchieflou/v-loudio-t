#pragma once

#include <JuceHeader.h>

/*
 * Minimal passthrough processor -- Epic 0 scaffolding only, proves the
 * VST3 build/load pipeline works before DSP work (Epic 2, issues #19-#26)
 * starts. Deliberately does not process audio yet beyond pass-through.
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

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoudioReverbProcessor)
};
