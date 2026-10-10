#pragma once

#include "LoudioProcessor.h"
#include "cuif/cuif.h"

#include <cmath>
#include <vector>

/*
 * Shared JUCE editor (P3 of the plugin platform, #129), extracted from the
 * Reverb's PluginEditor: hosts a cuif child window inside the JUCE
 * component, owns the plugin's widget tree, drives it from a 60 Hz timer,
 * debounces DPI-scale changes (#79) and bridges both rings.
 *
 * Traits contract (editor side, on top of LoudioProcessor.h's):
 *   using Ui = ...;                                       // opaque UI handle
 *   static constexpr int kPanelWidth, kPanelHeight;       // logical px
 *   static constexpr const char* kWindowTitle;
 *   static Ui*  uiCreate(float w, float h, void* context, loudio::vst::SendParamsFn sendParams);
 *   static void uiDestroy(Ui* ui);                        // safe on nullptr
 *   static cuif_widget* uiRoot(Ui* ui);
 *   static void uiSyncFromParams(Ui* ui, const float* params);   // never calls sendParams
 *   static void pollDspToUi(Ui* ui, cuif_spsc_ring_buffer& fromDsp);  // UI thread; drains Traits::afterProcess's frames
 */
namespace loudio {
namespace vst {

typedef void (*SendParamsFn)(void* context, const loudio::ParamUpdate* updates, size_t count);

constexpr const char* kFontPath = "C:\\Windows\\Fonts\\arial.ttf";
constexpr float kFontDesignSizePx = 13.0f;

template <class Traits>
class LoudioEditor : public juce::AudioProcessorEditor, public juce::Timer {
public:
    using Processor = LoudioProcessor<Traits>;
    using Ui = typename Traits::Ui;

    explicit LoudioEditor(Processor& p) : AudioProcessorEditor(&p), processorRef(p) {
        setSize(Traits::kPanelWidth, Traits::kPanelHeight);
        startTimerHz(60);
    }

    ~LoudioEditor() override {
        stopTimer();
        if (myWindow != nullptr) cuif_window_set_root_widget(myWindow, nullptr);
        Traits::uiDestroy(ui);
        if (myWindow != nullptr) cuif_window_destroy(myWindow);
    }

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::black); }

    void resized() override {
        ensureCuifWindowCreated();
        if (myWindow != nullptr) cuif_window_resize(myWindow, getWidth(), getHeight());
    }

    void timerCallback() override {
        ensureCuifWindowCreated();
        updateDpiScale();
        if (myWindow != nullptr) {
            if (ui != nullptr) Traits::pollDspToUi(ui, processorRef.dspToUiRingBuffer);
            cuif_window_pump(myWindow);
            cuif_window_render_frame(myWindow);
        }
    }

    void syncUIFromProcessor() {
        if (ui == nullptr) return;
        const loudio::ParamTable& table = Traits::Core::table();
        paramSnapshot.resize((size_t)table.count);
        for (int i = 0; i < table.count; ++i) {
            paramSnapshot[(size_t)i] = *processorRef.apvts.getRawParameterValue(table[i].id);
        }
        Traits::uiSyncFromParams(ui, paramSnapshot.data());
    }

    Processor& processorRef;

private:
    /* All-or-nothing UI -> DSP write (#107). cuif_spsc_write() does a
     * partial write when the ring is short on room, which would splice
     * half a ParamUpdate into the stream and desync every later record.
     * If there isn't room for `count` whole messages, drop the batch: the
     * UI may lose an intermediate value as long as the final settled value
     * gets through, and the UI thread must never block or spin. */
    static void sendParamUpdates(void* context, const loudio::ParamUpdate* msgs, size_t count) {
        auto* processor = static_cast<Processor*>(context);
        auto* rb = &processor->uiToDspRingBuffer;
        const size_t bytes = count * sizeof(loudio::ParamUpdate);
        if (cuif_spsc_writable(rb) < bytes) return;
        cuif_spsc_write(rb, (const unsigned char*)msgs, bytes);
    }

    /* getWindowHandle() returns null until this Component has a native
     * peer, which is not yet true during the constructor's setSize() (the
     * JUCE plugin wrapper attaches the peer afterwards), and resized() only
     * fires once in Standalone/most hosts (#62). So this runs every timer
     * tick and is a no-op once myWindow is set. */
    void ensureCuifWindowCreated() {
        if (myWindow != nullptr) return;

        void* parentHandle = getWindowHandle();
        if (parentHandle == nullptr) return;

        float initialScale = (float)juce::Component::getApproximateScaleFactorForComponent(this);
        if (initialScale <= 0.0f) initialScale = 1.0f;
        currentDpiScale = initialScale;
        pendingDpiScale = initialScale;
        pendingDpiScaleStableTicks = 0;

        cuif_window_desc desc = { 0 };
        desc.title = Traits::kWindowTitle;
        desc.width = getWidth();
        desc.height = getHeight();
        desc.parent_native_handle = parentHandle;
        desc.dpi_scale = currentDpiScale;

        myWindow = cuif_window_create(&desc);
        if (myWindow == nullptr) return;

        /* cuif_font_load() bakes at scale 1.0 (its "logical pixels"
         * contract); re-bake at once if the current scale differs, so text
         * is crisp from the first frame. */
        if (cuif_global_font == nullptr) {
            cuif_global_font = cuif_font_load(kFontPath, kFontDesignSizePx);
            if (cuif_global_font != nullptr && std::abs(currentDpiScale - 1.0f) > 0.001f) {
                cuif_font_rebake(cuif_global_font, cuif_font_effective_bake_px(kFontDesignSizePx, currentDpiScale));
            }
        }

        ui = Traits::uiCreate((float)getWidth(), (float)getHeight(), &processorRef, &LoudioEditor::sendParamUpdates);
        if (ui == nullptr) return;

        cuif_window_set_root_widget(myWindow, Traits::uiRoot(ui));
        syncUIFromProcessor();
    }

    /* Polls JUCE's own resolved DPI/host scale factor (not GetDpiForWindow,
     * which misses host-imposed AffineTransform scaling) and pushes it into
     * cuif once a change has been stable for kDpiScaleDebounceTicks, so a
     * live monitor-drag transition doesn't thrash the native window resize
     * and font re-bake every frame. */
    void updateDpiScale() {
        if (myWindow == nullptr) return;

        float scale = (float)juce::Component::getApproximateScaleFactorForComponent(this);
        if (scale <= 0.0f) scale = 1.0f;

        if (std::abs(scale - pendingDpiScale) > 0.001f) {
            pendingDpiScale = scale;
            pendingDpiScaleStableTicks = 0;
            return;
        }

        if (pendingDpiScaleStableTicks < kDpiScaleDebounceTicks) ++pendingDpiScaleStableTicks;

        if (pendingDpiScaleStableTicks == kDpiScaleDebounceTicks && std::abs(pendingDpiScale - currentDpiScale) > 0.001f) {
            currentDpiScale = pendingDpiScale;
            cuif_window_set_dpi_scale(myWindow, currentDpiScale);
            if (cuif_global_font != nullptr) {
                cuif_font_rebake(cuif_global_font, cuif_font_effective_bake_px(kFontDesignSizePx, currentDpiScale));
            }
        }
    }

    float currentDpiScale = 1.0f;
    float pendingDpiScale = 1.0f;
    int pendingDpiScaleStableTicks = 0;
    static constexpr int kDpiScaleDebounceTicks = 15; /* ~250 ms at 60 Hz */

    cuif_window* myWindow = nullptr;
    Ui* ui = nullptr;
    std::vector<float> paramSnapshot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoudioEditor)
};

template <class Traits>
juce::AudioProcessorEditor* LoudioProcessor<Traits>::createEditor() {
    return new LoudioEditor<Traits>(*this);
}

} // namespace vst
} // namespace loudio
