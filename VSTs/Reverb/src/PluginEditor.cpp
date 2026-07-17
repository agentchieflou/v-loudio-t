#include "PluginEditor.h"
#include <cmath>

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

    cuif_window_desc desc = { 0 };
    desc.title = "Reverb Panel";
    desc.width = getWidth();
    desc.height = getHeight();
    desc.parent_native_handle = parentHandle;

    myWindow = cuif_window_create(&desc);
    if (myWindow == nullptr) return;

    /* Initialize global font if not already set */
    extern cuif_font* cuif_global_font;
    if (cuif_global_font == nullptr) {
        cuif_global_font = cuif_font_load("C:\\Windows\\Fonts\\arial.ttf", 13.0f);
    }

    rootContainer = cuif_widget_create_container(0.0f, 0.0f, (float)getWidth(), (float)getHeight());
    rootContainer->user_data = this;

    /* 1. Add Rotary Knobs (X, Y, W, H, Label, DefaultValue, ChangeCallback) */
    knobs[kParamPreDelay] = cuif_widget_create_knob(30.0f, 70.0f, 70.0f, 70.0f, "Pre-Delay", 0.15f, knobChangedCallback);
    knobs[kParamDecayTime] = cuif_widget_create_knob(120.0f, 70.0f, 70.0f, 70.0f, "Decay Time", 0.2f, knobChangedCallback);
    knobs[kParamDamping] = cuif_widget_create_knob(210.0f, 70.0f, 70.0f, 70.0f, "Damping", 0.3f, knobChangedCallback);
    knobs[kParamWidth] = cuif_widget_create_knob(300.0f, 70.0f, 70.0f, 70.0f, "Width", 0.8f, knobChangedCallback);
    knobs[kParamDryWet] = cuif_widget_create_knob(390.0f, 70.0f, 70.0f, 70.0f, "Dry/Wet", 0.4f, knobChangedCallback);
    knobs[kParamDistance] = cuif_widget_create_knob(480.0f, 70.0f, 70.0f, 70.0f, "Distance", 0.5f, knobChangedCallback);
    knobs[kParamThickness] = cuif_widget_create_knob(570.0f, 70.0f, 70.0f, 70.0f, "Thickness", 0.5f, knobChangedCallback);

    for (int i = 0; i <= kParamThickness; ++i) {
        if (knobs[i]) {
            knobs[i]->user_data = (void*)(intptr_t)i;
            cuif_widget_add_child(rootContainer, knobs[i]);
        }
    }

    /* 2. Add Freeze Toggle Button */
    freezeButton = cuif_widget_create_button(680.0f, 85.0f, 80.0f, 30.0f, "FREEZE", true, buttonClickedCallback);
    freezeButton->user_data = (void*)(intptr_t)kParamFreeze;
    cuif_widget_add_child(rootContainer, freezeButton);

    /* 3. Add Mode Choice Dropdown */
    static const char* modesList[] = { "Room", "Hall", "Plate", "Cathedral", "Spring" };
    modeDropdown = cuif_widget_create_dropdown(650.0f, 15.0f, 120.0f, 30.0f, modesList, 5, dropdownChangedCallback);
    modeDropdown->user_data = (void*)(intptr_t)kParamMode;
    cuif_widget_add_child(rootContainer, modeDropdown);

    /* 4. Add Bezier EQ Node Editor */
    bezierEditor = cuif_widget_create_bezier_editor(30.0f, 180.0f, 340.0f, 220.0f, 3, bezierChangedCallback);
    bezierEditor->user_data = this;
    /* Anchor horizontal coordinates */
    bezierEditor->u.bezier_editor.node_x[0] = 0.1f;
    bezierEditor->u.bezier_editor.node_x[1] = 0.5f;
    bezierEditor->u.bezier_editor.node_x[2] = 0.9f;
    cuif_widget_add_child(rootContainer, bezierEditor);

    /* 5. Add Stereo Spectrogram Visualizers */
    analyzerLeft = cuif_widget_create_analyzer(430.0f, 180.0f, 165.0f, 220.0f, leftSpectrum, 64, cuif_rgba(0.0f, 0.75f, 0.70f, 0.8f));
    analyzerRight = cuif_widget_create_analyzer(605.0f, 180.0f, 165.0f, 220.0f, rightSpectrum, 64, cuif_rgba(0.85f, 0.65f, 0.15f, 0.6f));
    cuif_widget_add_child(rootContainer, analyzerLeft);
    cuif_widget_add_child(rootContainer, analyzerRight);

    /* 6. Add Post-EQ Gain Knobs */
    knobs[kParamPostEqLowGain] = cuif_widget_create_knob(50.0f, 420.0f, 70.0f, 70.0f, "Low EQ", 0.5f, knobChangedCallback);
    knobs[kParamPostEqMidGain] = cuif_widget_create_knob(140.0f, 420.0f, 70.0f, 70.0f, "Mid EQ", 0.5f, knobChangedCallback);
    knobs[kParamPostEqHighGain] = cuif_widget_create_knob(230.0f, 420.0f, 70.0f, 70.0f, "High EQ", 0.5f, knobChangedCallback);

    /* 7. Add Crossover Freq Knobs */
    knobs[kParamCrossoverLowMid] = cuif_widget_create_knob(320.0f, 420.0f, 70.0f, 70.0f, "Xover Low", 0.16f, knobChangedCallback);
    knobs[kParamCrossoverMidHigh] = cuif_widget_create_knob(410.0f, 420.0f, 70.0f, 70.0f, "Xover High", 0.33f, knobChangedCallback);

    /* 8. Add Ducking Knobs */
    knobs[kParamDuckThreshold] = cuif_widget_create_knob(500.0f, 420.0f, 70.0f, 70.0f, "Duck Thresh", 1.0f, knobChangedCallback);
    knobs[kParamDuckAmount] = cuif_widget_create_knob(590.0f, 420.0f, 70.0f, 70.0f, "Duck Depth", 0.0f, knobChangedCallback);
    knobs[kParamDuckRelease] = cuif_widget_create_knob(680.0f, 420.0f, 70.0f, 70.0f, "Duck Rel", 0.2f, knobChangedCallback);

    /* 9. Add Gate Knobs */
    knobs[kParamGateThreshold] = cuif_widget_create_knob(500.0f, 510.0f, 70.0f, 70.0f, "Gate Thresh", 0.0f, knobChangedCallback);
    knobs[kParamGateTime] = cuif_widget_create_knob(590.0f, 510.0f, 70.0f, 70.0f, "Gate Time", 0.2f, knobChangedCallback);

    for (int i = kParamPostEqLowGain; i < kNumParams; ++i) {
        if (knobs[i]) {
            knobs[i]->user_data = (void*)(intptr_t)i;
            cuif_widget_add_child(rootContainer, knobs[i]);
        }
    }

    cuif_window_set_root_widget(myWindow, rootContainer);
    syncUIFromProcessor();

    HWND childHwnd = (HWND)cuif_window_native_handle(myWindow);
    if (childHwnd != NULL) {
        MoveWindow(childHwnd, 0, 0, getWidth(), getHeight(), TRUE);
    }
}

void LoudioReverbEditor::resized() {
    ensureCuifWindowCreated();

    if (myWindow != nullptr) {
        HWND childHwnd = (HWND)cuif_window_native_handle(myWindow);
        if (childHwnd != NULL) {
            MoveWindow(childHwnd, 0, 0, getWidth(), getHeight(), TRUE);
        }
    }
}

void LoudioReverbEditor::timerCallback() {
    ensureCuifWindowCreated();

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

    if (bezierEditor) {
        float decayLow = *processorRef.apvts.getRawParameterValue("decayLow");
        float decayMid = *processorRef.apvts.getRawParameterValue("decayMid");
        float decayHigh = *processorRef.apvts.getRawParameterValue("decayHigh");

        bezierEditor->u.bezier_editor.node_y[0] = 1.0f - mapParamValueToWidget(kParamDecayLow, decayLow);
        bezierEditor->u.bezier_editor.node_y[1] = 1.0f - mapParamValueToWidget(kParamDecayMid, decayMid);
        bezierEditor->u.bezier_editor.node_y[2] = 1.0f - mapParamValueToWidget(kParamDecayHigh, decayHigh);
    }
}
