#include "PluginEditor.h"
#include "ReverbThemeMapping.h"
#include "ReverbLayout.h"
#include <cmath>
#include <cstring>

static float mapParamValueToWidget(int index, float rawVal) {
    switch (index) {
        case kParamPreDelay: return rawVal / 150.0f;
        case kParamDecayTime: return (rawVal - 0.4f) / 14.6f;
        case kParamDamping: return rawVal;
        case kParamWidth: return rawVal;
        case kParamDryWet: return rawVal;
        case kParamDistance: return rawVal;
        case kParamThickness: return rawVal;
        case kParamFreeze: return rawVal > 0.5f ? 1.0f : 0.0f;
        case kParamMode: return rawVal / 4.0f;
        case kParamDecayLow: return (rawVal - 0.1f) / 1.9f;
        case kParamDecayMid: return (rawVal - 0.1f) / 1.9f;
        case kParamDecayHigh: return (rawVal - 0.1f) / 1.9f;
        case kParamCrossoverLowMid: return (rawVal - 100.0f) / 900.0f;
        case kParamCrossoverMidHigh: return (rawVal - 1000.0f) / 9000.0f;
        case kParamPostEqLowGain: return (rawVal + 12.0f) / 24.0f;
        case kParamPostEqMidGain: return (rawVal + 12.0f) / 24.0f;
        case kParamPostEqHighGain: return (rawVal + 12.0f) / 24.0f;
        case kParamDuckThreshold: return (rawVal + 60.0f) / 60.0f;
        case kParamDuckAmount: return rawVal / 24.0f;
        case kParamDuckRelease: return (rawVal - 10.0f) / 990.0f;
        case kParamGateThreshold: return (rawVal + 80.0f) / 60.0f;
        case kParamGateTime: return (rawVal - 10.0f) / 490.0f;
        default: return 0.0f;
    }
}

static float mapWidgetValueToParam(int index, float widgetVal) {
    switch (index) {
        case kParamPreDelay: return widgetVal * 150.0f;
        case kParamDecayTime: return 0.4f + widgetVal * 14.6f;
        case kParamDamping: return widgetVal;
        case kParamWidth: return widgetVal;
        case kParamDryWet: return widgetVal;
        case kParamDistance: return widgetVal;
        case kParamThickness: return widgetVal;
        case kParamFreeze: return widgetVal > 0.5f ? 1.0f : 0.0f;
        case kParamMode: return std::round(widgetVal * 4.0f);
        case kParamDecayLow: return 0.1f + widgetVal * 1.9f;
        case kParamDecayMid: return 0.1f + widgetVal * 1.9f;
        case kParamDecayHigh: return 0.1f + widgetVal * 1.9f;
        case kParamCrossoverLowMid: return 100.0f + widgetVal * 900.0f;
        case kParamCrossoverMidHigh: return 1000.0f + widgetVal * 9000.0f;
        case kParamPostEqLowGain: return -12.0f + widgetVal * 24.0f;
        case kParamPostEqMidGain: return -12.0f + widgetVal * 24.0f;
        case kParamPostEqHighGain: return -12.0f + widgetVal * 24.0f;
        case kParamDuckThreshold: return -60.0f + widgetVal * 60.0f;
        case kParamDuckAmount: return widgetVal * 24.0f;
        case kParamDuckRelease: return 10.0f + widgetVal * 990.0f;
        case kParamGateThreshold: return -80.0f + widgetVal * 60.0f;
        case kParamGateTime: return 10.0f + widgetVal * 490.0f;
        default: return 0.0f;
    }
}

static void knobChangedCallback(cuif_widget* w, float val) {
    if (!w || !w->parent) return;
    auto* editor = static_cast<LoudioReverbEditor*>(w->parent->user_data);
    if (!editor) return;
    int paramIdx = (int)(intptr_t)w->user_data;
    float mappedVal = mapWidgetValueToParam(paramIdx, val);
    
    ParamUpdateMessage msg = { paramIdx, mappedVal };
    cuif_spsc_write(&editor->processorRef.uiToDspRingBuffer, (const unsigned char*)&msg, sizeof(ParamUpdateMessage));
}

static void dropdownChangedCallback(cuif_widget* w, float val) {
    if (!w || !w->parent) return;
    auto* editor = static_cast<LoudioReverbEditor*>(w->parent->user_data);
    if (!editor) return;
    int paramIdx = (int)(intptr_t)w->user_data;
    float mappedVal = mapWidgetValueToParam(paramIdx, val);
    
    ParamUpdateMessage msg = { paramIdx, mappedVal };
    cuif_spsc_write(&editor->processorRef.uiToDspRingBuffer, (const unsigned char*)&msg, sizeof(ParamUpdateMessage));
}

static void themeChangedCallback(cuif_widget* w, float val) {
    if (!w || !w->parent) return;
    auto* editor = static_cast<LoudioReverbEditor*>(w->parent->user_data);
    if (!editor) return;
    int themeIndex = (int)val;

    /* Apply immediately on the UI thread -- rendering happens here, not on the audio thread. */
    cuif_set_theme(themeForUiThemeIndex(themeIndex));

    /* Persist through the same ring-buffer -> APVTS path every other parameter uses. */
    ParamUpdateMessage msg = { kParamUiTheme, (float)themeIndex };
    cuif_spsc_write(&editor->processorRef.uiToDspRingBuffer, (const unsigned char*)&msg, sizeof(ParamUpdateMessage));
}

static void buttonClickedCallback(cuif_widget* w) {
    if (!w || !w->parent) return;
    auto* editor = static_cast<LoudioReverbEditor*>(w->parent->user_data);
    if (!editor) return;
    int paramIdx = (int)(intptr_t)w->user_data;
    float widgetVal = w->u.button.state ? 1.0f : 0.0f;
    float mappedVal = mapWidgetValueToParam(paramIdx, widgetVal);
    
    ParamUpdateMessage msg = { paramIdx, mappedVal };
    cuif_spsc_write(&editor->processorRef.uiToDspRingBuffer, (const unsigned char*)&msg, sizeof(ParamUpdateMessage));
}

static void bezierChangedCallback(cuif_widget* w) {
    if (!w || !w->parent) return;
    auto* editor = static_cast<LoudioReverbEditor*>(w->parent->user_data);
    if (!editor) return;

    float valLow = 1.0f - w->u.bezier_editor.node_y[0];
    float valMid = 1.0f - w->u.bezier_editor.node_y[1];
    float valHigh = 1.0f - w->u.bezier_editor.node_y[2];

    float decayLow = 0.1f + valLow * 1.9f;
    float decayMid = 0.1f + valMid * 1.9f;
    float decayHigh = 0.1f + valHigh * 1.9f;

    ParamUpdateMessage msgLow = { kParamDecayLow, decayLow };
    ParamUpdateMessage msgMid = { kParamDecayMid, decayMid };
    ParamUpdateMessage msgHigh = { kParamDecayHigh, decayHigh };

    cuif_spsc_write(&editor->processorRef.uiToDspRingBuffer, (const unsigned char*)&msgLow, sizeof(ParamUpdateMessage));
    cuif_spsc_write(&editor->processorRef.uiToDspRingBuffer, (const unsigned char*)&msgMid, sizeof(ParamUpdateMessage));
    cuif_spsc_write(&editor->processorRef.uiToDspRingBuffer, (const unsigned char*)&msgHigh, sizeof(ParamUpdateMessage));
}

LoudioReverbEditor::LoudioReverbEditor(LoudioReverbProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p) {
    
    std::fill(std::begin(leftSpectrum), std::end(leftSpectrum), 0.0f);
    std::fill(std::begin(rightSpectrum), std::end(rightSpectrum), 0.0f);

    setSize(800, 600);
    startTimerHz(60);
}

LoudioReverbEditor::~LoudioReverbEditor() {
    stopTimer();
    if (myWindow != nullptr) {
        cuif_window_destroy(myWindow);
    }
}

void LoudioReverbEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black);
}

static const ReverbWidgetRect& findRect(const std::vector<ReverbWidgetRect>& layout, const char* id) {
    for (const auto& r : layout) {
        if (std::strcmp(r.id, id) == 0) return r;
    }
    jassertfalse; // id not present in the layout -- ReverbLayout.h and this file have drifted apart
    static ReverbWidgetRect fallback{ "missing", ReverbLayoutGroup::Global, 0.0f, 0.0f, 10.0f, 10.0f };
    return fallback;
}

static void tabChangedCallback(cuif_widget* w, float value) {
    if (!w || !w->parent) return;
    auto* editor = static_cast<LoudioReverbEditor*>(w->parent->user_data);
    if (!editor) return;

    bool showAdvanced = ((int)value == 1);
    if (editor->mainPage) editor->mainPage->visible = !showAdvanced;
    if (editor->advancedPage) editor->advancedPage->visible = showAdvanced;
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

    /* Initialize global font if not already set */
    extern cuif_font* cuif_global_font;
    if (cuif_global_font == nullptr) {
        cuif_global_font = cuif_font_load("C:\\Windows\\Fonts\\arial.ttf", 13.0f);
    }

    rootContainer = cuif_widget_create_container(0.0f, 0.0f, (float)getWidth(), (float)getHeight());
    rootContainer->user_data = this;

    auto layout = computeReverbLayout((float)getWidth(), (float)getHeight());

    /* Page containers -- Main starts visible, Advanced starts hidden. Reused whenever the tab changes. */
    mainPage = cuif_widget_create_container(0.0f, 0.0f, (float)getWidth(), (float)getHeight());
    mainPage->visible = true;
    cuif_widget_add_child(rootContainer, mainPage);

    advancedPage = cuif_widget_create_container(0.0f, 0.0f, (float)getWidth(), (float)getHeight());
    advancedPage->visible = false;
    cuif_widget_add_child(rootContainer, advancedPage);

    /* 1. Global header: tab bar, mode dropdown, theme dropdown, freeze button (#69) */
    static const char* tabsList[] = { "Main", "Advanced" };
    {
        const auto& r = findRect(layout, "tabbar");
        tabBar = cuif_widget_create_tabbar(r.x, r.y, r.w, r.h, tabsList, 2, tabChangedCallback);
        cuif_widget_add_child(rootContainer, tabBar);
    }

    static const char* modesList[] = { "Room", "Hall", "Plate", "Cathedral", "Spring" };
    {
        const auto& r = findRect(layout, "dropdown.mode");
        modeDropdown = cuif_widget_create_dropdown(r.x, r.y, r.w, r.h, modesList, 5, dropdownChangedCallback);
        modeDropdown->user_data = (void*)(intptr_t)kParamMode;
        cuif_widget_add_child(rootContainer, modeDropdown);
    }

    static const char* themesList[] = { "Default", "Hello Kitty", "Greens" };
    {
        const auto& r = findRect(layout, "dropdown.theme");
        themeDropdown = cuif_widget_create_dropdown(r.x, r.y, r.w, r.h, themesList, 3, themeChangedCallback);
        themeDropdown->user_data = (void*)(intptr_t)kParamUiTheme;
        cuif_widget_add_child(rootContainer, themeDropdown);
    }

    {
        const auto& r = findRect(layout, "button.freeze");
        freezeButton = cuif_widget_create_button(r.x, r.y, r.w, r.h, "FREEZE", true, buttonClickedCallback);
        freezeButton->user_data = (void*)(intptr_t)kParamFreeze;
        cuif_widget_add_child(rootContainer, freezeButton);
    }

    /* 2. Main tab: hero decay knob, secondary knob row, decay-EQ graph, analyzers */
    auto addMainKnob = [&](int paramIndex, const char* id, const char* label, float defaultVal) {
        const auto& r = findRect(layout, id);
        knobs[paramIndex] = cuif_widget_create_knob(r.x, r.y, r.w, r.h, label, defaultVal, knobChangedCallback);
        knobs[paramIndex]->user_data = (void*)(intptr_t)paramIndex;
        cuif_widget_add_child(mainPage, knobs[paramIndex]);
    };

    addMainKnob(kParamDecayTime, "knob.decayTime", "Decay Time", 0.2f);
    addMainKnob(kParamPreDelay, "knob.preDelay", "Pre-Delay", 0.15f);
    addMainKnob(kParamDamping, "knob.damping", "Damping", 0.3f);
    addMainKnob(kParamWidth, "knob.width", "Width", 0.8f);
    addMainKnob(kParamDryWet, "knob.dryWet", "Dry/Wet", 0.4f);
    addMainKnob(kParamDistance, "knob.distance", "Distance", 0.5f);
    addMainKnob(kParamThickness, "knob.thickness", "Thickness", 0.5f);

    {
        const auto& r = findRect(layout, "bezier.decayEq");
        bezierEditor = cuif_widget_create_bezier_editor(r.x, r.y, r.w, r.h, 3, bezierChangedCallback);
        bezierEditor->user_data = this;
        bezierEditor->u.bezier_editor.node_x[0] = 0.1f;
        bezierEditor->u.bezier_editor.node_x[1] = 0.5f;
        bezierEditor->u.bezier_editor.node_x[2] = 0.9f;
        cuif_widget_add_child(mainPage, bezierEditor);
    }

    {
        const auto& r = findRect(layout, "analyzer.left");
        analyzerLeft = cuif_widget_create_analyzer(r.x, r.y, r.w, r.h, leftSpectrum, 64, cuif_rgba(0.0f, 0.75f, 0.70f, 0.8f));
        cuif_widget_add_child(mainPage, analyzerLeft);
    }
    {
        const auto& r = findRect(layout, "analyzer.right");
        analyzerRight = cuif_widget_create_analyzer(r.x, r.y, r.w, r.h, rightSpectrum, 64, cuif_rgba(0.85f, 0.65f, 0.15f, 0.6f));
        cuif_widget_add_child(mainPage, analyzerRight);
    }

    /* 3. Advanced tab: post EQ, crossover, ducking, gate */
    auto addAdvancedKnob = [&](int paramIndex, const char* id, const char* label, float defaultVal) {
        const auto& r = findRect(layout, id);
        knobs[paramIndex] = cuif_widget_create_knob(r.x, r.y, r.w, r.h, label, defaultVal, knobChangedCallback);
        knobs[paramIndex]->user_data = (void*)(intptr_t)paramIndex;
        cuif_widget_add_child(advancedPage, knobs[paramIndex]);
    };

    addAdvancedKnob(kParamPostEqLowGain, "knob.postEqLowGain", "Low EQ", 0.5f);
    addAdvancedKnob(kParamPostEqMidGain, "knob.postEqMidGain", "Mid EQ", 0.5f);
    addAdvancedKnob(kParamPostEqHighGain, "knob.postEqHighGain", "High EQ", 0.5f);
    addAdvancedKnob(kParamCrossoverLowMid, "knob.crossoverLowMid", "Xover Low", 0.16f);
    addAdvancedKnob(kParamCrossoverMidHigh, "knob.crossoverMidHigh", "Xover High", 0.33f);
    addAdvancedKnob(kParamDuckThreshold, "knob.duckThreshold", "Duck Thresh", 1.0f);
    addAdvancedKnob(kParamDuckAmount, "knob.duckAmount", "Duck Depth", 0.0f);
    addAdvancedKnob(kParamDuckRelease, "knob.duckRelease", "Duck Rel", 0.2f);
    addAdvancedKnob(kParamGateThreshold, "knob.gateThreshold", "Gate Thresh", 0.0f);
    addAdvancedKnob(kParamGateTime, "knob.gateTime", "Gate Time", 0.2f);

    cuif_window_set_root_widget(myWindow, rootContainer);
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
    unsigned char chId;
    while (cuif_spsc_readable(&processorRef.dspToUiRingBuffer) >= sizeof(chId) + 64 * sizeof(float)) {
        size_t read_bytes = cuif_spsc_read(&processorRef.dspToUiRingBuffer, &chId, sizeof(chId));
        if (read_bytes == sizeof(chId)) {
            if (chId == 'L') {
                cuif_spsc_read(&processorRef.dspToUiRingBuffer, (unsigned char*)leftSpectrum, 64 * sizeof(float));
            } else if (chId == 'R') {
                cuif_spsc_read(&processorRef.dspToUiRingBuffer, (unsigned char*)rightSpectrum, 64 * sizeof(float));
            }
        }
    }
}

void LoudioReverbEditor::syncUIFromProcessor() {
    for (int i = 0; i < kNumParams; ++i) {
        if (knobs[i] != nullptr) {
            const char* paramId = nullptr;
            switch (i) {
                case kParamPreDelay: paramId = "preDelay"; break;
                case kParamDecayTime: paramId = "decayTime"; break;
                case kParamDamping: paramId = "damping"; break;
                case kParamWidth: paramId = "width"; break;
                case kParamDryWet: paramId = "dryWet"; break;
                case kParamDistance: paramId = "distance"; break;
                case kParamThickness: paramId = "thickness"; break;
                case kParamDecayLow: paramId = "decayLow"; break;
                case kParamDecayMid: paramId = "decayMid"; break;
                case kParamDecayHigh: paramId = "decayHigh"; break;
                case kParamCrossoverLowMid: paramId = "crossoverLowMid"; break;
                case kParamCrossoverMidHigh: paramId = "crossoverMidHigh"; break;
                case kParamPostEqLowGain: paramId = "postEqLowGain"; break;
                case kParamPostEqMidGain: paramId = "postEqMidGain"; break;
                case kParamPostEqHighGain: paramId = "postEqHighGain"; break;
                case kParamDuckThreshold: paramId = "duckThreshold"; break;
                case kParamDuckAmount: paramId = "duckAmount"; break;
                case kParamDuckRelease: paramId = "duckRelease"; break;
                case kParamGateThreshold: paramId = "gateThreshold"; break;
                case kParamGateTime: paramId = "gateTime"; break;
                default: break;
            }
            if (paramId) {
                float rawVal = *processorRef.apvts.getRawParameterValue(paramId);
                knobs[i]->u.knob.value = mapParamValueToWidget(i, rawVal);
            }
        }
    }

    if (freezeButton) {
        float rawVal = *processorRef.apvts.getRawParameterValue("freeze");
        freezeButton->u.button.state = rawVal > 0.5f;
    }

    if (modeDropdown) {
        float rawVal = *processorRef.apvts.getRawParameterValue("mode");
        modeDropdown->u.dropdown.selected_index = (int)rawVal;
    }

    if (themeDropdown) {
        int themeIndex = (int)*processorRef.apvts.getRawParameterValue("uiTheme");
        themeDropdown->u.dropdown.selected_index = themeIndex;
        cuif_set_theme(themeForUiThemeIndex(themeIndex));
    }

    if (bezierEditor) {
        float decayLow = *processorRef.apvts.getRawParameterValue("decayLow");
        float decayMid = *processorRef.apvts.getRawParameterValue("decayMid");
        float decayHigh = *processorRef.apvts.getRawParameterValue("decayHigh");

        bezierEditor->u.bezier_editor.node_y[0] = 1.0f - mapParamValueToWidget(kParamDecayLow, decayLow);
        bezierEditor->u.bezier_editor.node_y[1] = 1.0f - mapParamValueToWidget(kParamDecayMid, decayMid);
        bezierEditor->u.bezier_editor.node_y[2] = 1.0f - mapParamValueToWidget(kParamDecayHigh, decayHigh);
    }
}
