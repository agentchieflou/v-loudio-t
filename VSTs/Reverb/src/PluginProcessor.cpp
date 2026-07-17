#include "PluginProcessor.h"
#include "PluginEditor.h"

LoudioReverbProcessor::LoudioReverbProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
}

void LoudioReverbProcessor::prepareToPlay(double, int) {
}

void LoudioReverbProcessor::releaseResources() {
}

bool LoudioReverbProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

void LoudioReverbProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) {
    /* Passthrough: input is left untouched. Real DSP lands in Epic 2. */
}

juce::AudioProcessorEditor* LoudioReverbProcessor::createEditor() {
    return new LoudioReverbEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new LoudioReverbProcessor();
}
