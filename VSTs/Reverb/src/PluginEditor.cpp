#include "PluginEditor.h"

LoudioReverbEditor::LoudioReverbEditor(LoudioReverbProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p) {
    setSize(400, 300);
}

void LoudioReverbEditor::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.setColour(juce::Colours::white);
    g.setFont(18.0f);
    g.drawFittedText("Loudio Reverb -- scaffold build", getLocalBounds(), juce::Justification::centred, 2);
}

void LoudioReverbEditor::resized() {
}
