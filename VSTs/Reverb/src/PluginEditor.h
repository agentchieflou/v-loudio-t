#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "cuif/cuif.h"
#include "SharedStructures.h"

class LoudioReverbEditor : public juce::AudioProcessorEditor, public juce::Timer {
public:
    explicit LoudioReverbEditor(LoudioReverbProcessor&);
    ~LoudioReverbEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

    void pollSpectrumData();
    void syncUIFromProcessor();

    LoudioReverbProcessor& processorRef;

    /*
     * Touched from free-function widget callbacks (tabChangedCallback), same
     * as processorRef above -- not part of the class's own public API.
     */
    cuif_widget* mainPage = nullptr;
    cuif_widget* advancedPage = nullptr;

private:
    void ensureCuifWindowCreated();

    cuif_window* myWindow = nullptr;
    cuif_widget* rootContainer = nullptr;

    /* Array containing knob pointers mapped by ReverbParamIndex */
    cuif_widget* knobs[kNumParams] = { nullptr };
    cuif_widget* freezeButton = nullptr;
    cuif_widget* modeDropdown = nullptr;
    cuif_widget* themeDropdown = nullptr;
    cuif_widget* tabBar = nullptr;
    cuif_widget* bezierEditor = nullptr;
    cuif_widget* analyzerLeft = nullptr;
    cuif_widget* analyzerRight = nullptr;

    float leftSpectrum[64];
    float rightSpectrum[64];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoudioReverbEditor)
};
