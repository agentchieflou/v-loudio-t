#include "PluginEditor.h"
#include <cmath>
#include <cstring>

extern cuif_font* cuif_global_font;
static constexpr float kFontDesignSizePx = 13.0f;

/*
 * All-or-nothing UI -> DSP write (#107). cuif_spsc_write() does a partial
 * write when the ring is short on room, which would splice half a
 * ParamUpdateMessage into the stream and desync every later record. If
 * there isn't room for `count` whole messages, drop the whole batch: the UI
 * may lose an intermediate value as long as the final settled value gets
 * through, and the UI thread must never block or spin on user input.
 * Installed as the shared ReverbUi's sendParams callback.
 */
static void sendParamUpdates(void* context, const ParamUpdateMessage* msgs, size_t count) {
    auto* processor = static_cast<LoudioReverbProcessor*>(context);
    auto* rb = &processor->uiToDspRingBuffer;
    const size_t bytes = count * sizeof(ParamUpdateMessage);
    if (cuif_spsc_writable(rb) < bytes) return;
    cuif_spsc_write(rb, (const unsigned char*)msgs, bytes);
}

LoudioReverbEditor::LoudioReverbEditor(LoudioReverbProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p) {
    
    setSize(800, 600);
    startTimerHz(60);
}

LoudioReverbEditor::~LoudioReverbEditor() {
    stopTimer();
    if (myWindow != nullptr) {
        cuif_window_set_root_widget(myWindow, nullptr);
    }
    /* The widget tree used to be leaked here on every editor close. */
    reverbUiDestroy(ui);
    if (myWindow != nullptr) {
        cuif_window_destroy(myWindow);
    }
}

void LoudioReverbEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black);
}

/*
 * getWindowHandle() returns null until this Component has an actual native
 * peer -- which is not yet true during the constructor's setSize() call
 * (the JUCE plugin wrapper attaches the peer afterwards). resized() only
 * fires once in Standalone/most hosts with no further size change, so
 * gating window creation there alone left it permanently null (issue #62).
 * This is called every timer tick instead, and is a no-op once myWindow is set.
 */
void LoudioReverbEditor::ensureCuifWindowCreated() {
    if (myWindow != nullptr) return;

    void* parentHandle = getWindowHandle();
    if (parentHandle == nullptr) return;

    float initialScale = (float)juce::Component::getApproximateScaleFactorForComponent(this);
    if (initialScale <= 0.0f) initialScale = 1.0f;
    currentDpiScale = initialScale;
    pendingDpiScale = initialScale;
    pendingDpiScaleStableTicks = 0;

    cuif_window_desc desc = { 0 };
    desc.title = "Reverb Panel";
    desc.width = getWidth();
    desc.height = getHeight();
    desc.parent_native_handle = parentHandle;
    desc.dpi_scale = currentDpiScale;

    myWindow = cuif_window_create(&desc);
    if (myWindow == nullptr) return;

    /* Initialize global font if not already set. cuif_font_load() bakes at
       its own scale=1.0 (per its documented "logical pixels" contract);
       re-bake immediately below if the actual current scale differs, so
       text is crisp for the current display from the very first frame. */
    if (cuif_global_font == nullptr) {
        cuif_global_font = cuif_font_load("C:\\Windows\\Fonts\\arial.ttf", kFontDesignSizePx);
        if (cuif_global_font != nullptr && std::abs(currentDpiScale - 1.0f) > 0.001f) {
            cuif_font_rebake(cuif_global_font, cuif_font_effective_bake_px(kFontDesignSizePx, currentDpiScale));
        }
    }

    ui = reverbUiCreate((float)getWidth(), (float)getHeight(), { &processorRef, sendParamUpdates });
    if (ui == nullptr) return;

    cuif_window_set_root_widget(myWindow, ui->root);
    syncUIFromProcessor();
}

void LoudioReverbEditor::resized() {
    ensureCuifWindowCreated();

    if (myWindow != nullptr) {
        cuif_window_resize(myWindow, getWidth(), getHeight());
    }
}

/*
 * Polls JUCE's own resolved DPI/host scale factor (not GetDpiForWindow --
 * that would only see the OS per-monitor DPI and miss host-imposed
 * AffineTransform scaling in a non-per-monitor-aware VST3 host) and pushes
 * it into cuif once a change has been stable for kDpiScaleDebounceTicks
 * ticks, so a live monitor-drag transition doesn't thrash the native window
 * resize (and, once the font atlas re-bakes on scale change, an expensive
 * re-bake) every single frame.
 */
void LoudioReverbEditor::updateDpiScale() {
    if (myWindow == nullptr) return;

    float scale = (float)juce::Component::getApproximateScaleFactorForComponent(this);
    if (scale <= 0.0f) scale = 1.0f;

    if (std::abs(scale - pendingDpiScale) > 0.001f) {
        pendingDpiScale = scale;
        pendingDpiScaleStableTicks = 0;
        return;
    }

    if (pendingDpiScaleStableTicks < kDpiScaleDebounceTicks) {
        ++pendingDpiScaleStableTicks;
    }

    if (pendingDpiScaleStableTicks == kDpiScaleDebounceTicks && std::abs(pendingDpiScale - currentDpiScale) > 0.001f) {
        currentDpiScale = pendingDpiScale;
        cuif_window_set_dpi_scale(myWindow, currentDpiScale);

        if (cuif_global_font != nullptr) {
            cuif_font_rebake(cuif_global_font, cuif_font_effective_bake_px(kFontDesignSizePx, currentDpiScale));
        }
    }
}

void LoudioReverbEditor::timerCallback() {
    ensureCuifWindowCreated();
    updateDpiScale();

    if (myWindow != nullptr) {
        pollSpectrumData();
        cuif_window_pump(myWindow);
        cuif_window_render_frame(myWindow);
    }
}

void LoudioReverbEditor::pollSpectrumData() {
    if (ui == nullptr) return;
    unsigned char chId;
    while (cuif_spsc_readable(&processorRef.dspToUiRingBuffer) >= sizeof(chId) + 64 * sizeof(float)) {
        size_t read_bytes = cuif_spsc_read(&processorRef.dspToUiRingBuffer, &chId, sizeof(chId));
        if (read_bytes == sizeof(chId)) {
            if (chId == 'L') {
                cuif_spsc_read(&processorRef.dspToUiRingBuffer, (unsigned char*)ui->leftSpectrum, 64 * sizeof(float));
            } else if (chId == 'R') {
                cuif_spsc_read(&processorRef.dspToUiRingBuffer, (unsigned char*)ui->rightSpectrum, 64 * sizeof(float));
            }
        }
    }
}

void LoudioReverbEditor::syncUIFromProcessor() {
    if (ui == nullptr) return;
    float params[kNumParams];
    for (int i = 0; i < kNumParams; ++i) {
        params[i] = *processorRef.apvts.getRawParameterValue(kReverbParams[i].id);
    }
    reverbUiSyncFromParams(ui, params);
}
