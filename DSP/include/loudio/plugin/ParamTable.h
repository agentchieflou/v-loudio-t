#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>

/*
 * Plugin-agnostic parameter table (P1 of the plugin platform, #127).
 *
 * A plugin declares ONE static array of ParamMeta -- the single source of
 * truth for every parameter's stable id string, display name, range,
 * default, step and flags -- and wraps it in a ParamTable. Everything
 * else (the LPI wrapper's lpi_parameter_info, the JUCE wrapper's
 * createParameterLayout(), the state blob, the UI mapping) reads from that
 * one list, so the numbers can't drift the way two hand-maintained lists
 * eventually would.
 *
 * Bool/choice parameters are represented the same way as every other
 * parameter: a plain float range plus a flag. Format-specific display
 * concerns (JUCE's AudioParameterChoice labels, LPI's LPI_PARAM_STEPPED)
 * are layered on top by the wrappers.
 *
 * Order matters: entries[i].index must equal i (checked by
 * ParamTable::indicesAreConsistent(); each plugin's tests assert it).
 */
namespace loudio {

/* Values match PluginABI's LPI_PARAM_* bit for bit so an LPI wrapper can
 * forward `flags` unchanged (lpi_reverb.cpp static_asserts this). */
enum ParamFlags : uint32_t {
    kParamBoolean  = 1u << 0, /* value is conceptually 0.0/1.0 */
    kParamStepped  = 1u << 1, /* value snaps to integer steps between min/max */
    kParamReadOnly = 1u << 2  /* host-visible meter/output, not user-settable */
};

struct ParamMeta {
    int index;
    const char* id;    /* stable, never renamed: state blobs key on it */
    const char* name;  /* display name */
    float minValue;
    float maxValue;
    float defaultValue;
    float stepValue;   /* UI/automation granularity hint; ignored for Boolean/Stepped */
    uint32_t flags;    /* bitwise OR of ParamFlags */
};

/* One parameter update, as carried by ParamQueue and by every UI->DSP
 * message path in the codebase. Plain {int, float}: 8 bytes, trivially
 * copyable, so it can be memcpy'd through a byte ring buffer. */
struct ParamUpdate {
    int index;
    float value;
};

struct ParamTable {
    const ParamMeta* entries;
    int count;

    bool valid(int index) const { return index >= 0 && index < count; }

    const ParamMeta& operator[](int index) const { return entries[index]; }

    bool indicesAreConsistent() const {
        for (int i = 0; i < count; ++i) {
            if (entries[i].index != i) return false;
        }
        return true;
    }

    /* -1 when no entry has that id. `len` lets a caller match an id that
     * isn't NUL-terminated (a state blob entry). */
    int findById(const char* id, size_t len) const {
        if (!id) return -1;
        for (int i = 0; i < count; ++i) {
            if (std::strlen(entries[i].id) == len && std::memcmp(entries[i].id, id, len) == 0) return i;
        }
        return -1;
    }
    int findById(const char* id) const { return id ? findById(id, std::strlen(id)) : -1; }

    /* Clamps to [min, max]; non-finite -> default. Requires a valid index. */
    float sanitize(int index, float value) const {
        const ParamMeta& m = entries[index];
        if (!std::isfinite(value)) return m.defaultValue;
        if (value < m.minValue) return m.minValue;
        if (value > m.maxValue) return m.maxValue;
        return value;
    }

    /* Linear parameter <-> widget (0..1) mapping over [min, max]. Boolean
     * parameters threshold at 0.5 on the way back; Stepped ones round to
     * the nearest integer step. Dropdown-style parameters whose widget
     * already reports the index itself don't need these. */
    float toWidget(int index, float value) const {
        const ParamMeta& m = entries[index];
        float w = (value - m.minValue) / (m.maxValue - m.minValue);
        return w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w);
    }

    float fromWidget(int index, float widgetValue) const {
        const ParamMeta& m = entries[index];
        if (m.flags & kParamBoolean) return widgetValue > 0.5f ? 1.0f : 0.0f;
        float v = m.minValue + widgetValue * (m.maxValue - m.minValue);
        if (m.flags & kParamStepped) v = std::round(v);
        return v;
    }

    void fillDefaults(float* values) const {
        for (int i = 0; i < count; ++i) values[i] = entries[i].defaultValue;
    }
};

} // namespace loudio
