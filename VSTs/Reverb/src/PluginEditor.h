#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "cuif/cuif.h"
#include "SharedStructures.h"
#include "ReverbUi.h"

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

private:
    void ensureCuifWindowCreated();
    void updateDpiScale();

    /*
     * DPI-scale change detection, debounced so a live monitor-drag
     * transition doesn't thrash the native window resize (and, once #83
     * lands, an expensive font atlas re-bake) every single frame.
     */
    float currentDpiScale = 1.0f;
    float pendingDpiScale = 1.0f;
    int pendingDpiScaleStableTicks = 0;
    static constexpr int kDpiScaleDebounceTicks = 15; // ~250ms at the editor's 60Hz timer

    cuif_window* myWindow = nullptr;

    /* The shared widget tree (ReverbUi.h), also used by the LPI wrapper's
     * offscreen editor. Owned here; created with the window. */
    ReverbUi* ui = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoudioReverbEditor)
};
