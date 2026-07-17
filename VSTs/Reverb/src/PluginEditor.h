#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

/*
 * Placeholder editor -- Epic 0 scaffolding only. Gets replaced by the
 * cuif-based editor in Epic 5 (JUCE<->Framework bridge, issue: "JUCE <->
 * Framework bridge"), which embeds a Framework/ native window inside this
 * editor's host-provided parent window handle.
 */
class LoudioReverbEditor : public juce::AudioProcessorEditor {
public:
    explicit LoudioReverbEditor(LoudioReverbProcessor&);
    ~LoudioReverbEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    LoudioReverbProcessor& processorRef;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoudioReverbEditor)
};
