#pragma once

#include <vector>

/*
 * Pure layout computation for the Reverb panel -- no JUCE dependency, so it
 * can be unit-tested headlessly from DSPTests.cpp (no-overlap, in-bounds
 * assertions) instead of only being checkable by eye in a screenshot. This
 * is what PluginEditor.cpp's ensureCuifWindowCreated() calls to get widget
 * rects, rather than hardcoding coordinates inline (which is how the
 * overlapping-analyzers bug, #64, happened in the first place).
 */

enum class ReverbLayoutGroup {
    Global,   // always visible, regardless of which tab is active
    Main,     // only visible on the "Main" tab
    Advanced  // only visible on the "Advanced" tab
};

struct ReverbWidgetRect {
    const char* id;  // stable identifier, e.g. "knob.preDelay", "tabbar", "analyzer.left"
    ReverbLayoutGroup group;
    float x, y, w, h;
};

inline std::vector<ReverbWidgetRect> computeReverbLayout(float panelWidth, float panelHeight) {
    (void)panelHeight; // current layout is fixed-size within the default panel; reflow is future work

    std::vector<ReverbWidgetRect> r;

    /* ---- Global header row (always visible, y = 5..35) ---- */
    r.push_back({ "tabbar",            ReverbLayoutGroup::Global, 10.0f,  5.0f, 180.0f, 30.0f });
    r.push_back({ "dropdown.mode",     ReverbLayoutGroup::Global, panelWidth - 320.0f, 5.0f, 110.0f, 30.0f });
    r.push_back({ "dropdown.theme",    ReverbLayoutGroup::Global, panelWidth - 200.0f, 5.0f, 110.0f, 30.0f });
    r.push_back({ "button.freeze",     ReverbLayoutGroup::Global, panelWidth - 80.0f,  5.0f, 70.0f,  30.0f });

    /* ---- Main tab: hero decay/space knob + secondary row + decay-EQ graph + analyzers ---- */
    r.push_back({ "knob.decayTime",    ReverbLayoutGroup::Main, 345.0f, 50.0f, 110.0f, 110.0f });

    r.push_back({ "knob.preDelay",     ReverbLayoutGroup::Main, 30.0f,  180.0f, 70.0f, 70.0f });
    r.push_back({ "knob.damping",      ReverbLayoutGroup::Main, 120.0f, 180.0f, 70.0f, 70.0f });
    r.push_back({ "knob.width",        ReverbLayoutGroup::Main, 210.0f, 180.0f, 70.0f, 70.0f });
    r.push_back({ "knob.dryWet",       ReverbLayoutGroup::Main, 300.0f, 180.0f, 70.0f, 70.0f });
    r.push_back({ "knob.distance",     ReverbLayoutGroup::Main, 390.0f, 180.0f, 70.0f, 70.0f });
    r.push_back({ "knob.thickness",    ReverbLayoutGroup::Main, 480.0f, 180.0f, 70.0f, 70.0f });

    r.push_back({ "bezier.decayEq",    ReverbLayoutGroup::Main, 30.0f,  270.0f, 340.0f, 220.0f });
    r.push_back({ "analyzer.left",     ReverbLayoutGroup::Main, 400.0f, 270.0f, 165.0f, 220.0f });
    r.push_back({ "analyzer.right",    ReverbLayoutGroup::Main, 575.0f, 270.0f, 165.0f, 220.0f });

    /* ---- Advanced tab: post EQ, crossover, ducking, gate ---- */
    r.push_back({ "knob.postEqLowGain",     ReverbLayoutGroup::Advanced, 30.0f,  70.0f, 70.0f, 70.0f });
    r.push_back({ "knob.postEqMidGain",     ReverbLayoutGroup::Advanced, 120.0f, 70.0f, 70.0f, 70.0f });
    r.push_back({ "knob.postEqHighGain",    ReverbLayoutGroup::Advanced, 210.0f, 70.0f, 70.0f, 70.0f });
    r.push_back({ "knob.crossoverLowMid",   ReverbLayoutGroup::Advanced, 320.0f, 70.0f, 70.0f, 70.0f });
    r.push_back({ "knob.crossoverMidHigh",  ReverbLayoutGroup::Advanced, 410.0f, 70.0f, 70.0f, 70.0f });

    r.push_back({ "knob.duckThreshold",     ReverbLayoutGroup::Advanced, 30.0f,  180.0f, 70.0f, 70.0f });
    r.push_back({ "knob.duckAmount",        ReverbLayoutGroup::Advanced, 120.0f, 180.0f, 70.0f, 70.0f });
    r.push_back({ "knob.duckRelease",       ReverbLayoutGroup::Advanced, 210.0f, 180.0f, 70.0f, 70.0f });
    r.push_back({ "knob.gateThreshold",     ReverbLayoutGroup::Advanced, 320.0f, 180.0f, 70.0f, 70.0f });
    r.push_back({ "knob.gateTime",          ReverbLayoutGroup::Advanced, 410.0f, 180.0f, 70.0f, 70.0f });

    return r;
}
