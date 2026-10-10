#include "cuif/widget.h"
#include "cuif/graphics.h"
#include "cuif/theme.h"
#include "cuif/tessellation.h"
#include <windows.h>
#include <gl/gl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "cuif/window.h"

cuif_font* cuif_global_font = NULL;

static cuif_widget* cuif_widget_create_base(cuif_widget_type type, float x, float y, float w, float h) {
    cuif_widget* widget = (cuif_widget*)calloc(1, sizeof(cuif_widget));
    if (!widget) return NULL;
    widget->type = type;
    widget->x = x;
    widget->y = y;
    widget->w = w;
    widget->h = h;
    widget->visible = true;
    widget->enabled = true;
    return widget;
}

cuif_widget* cuif_widget_create_container(float x, float y, float w, float h) {
    return cuif_widget_create_base(CUIF_WIDGET_CONTAINER, x, y, w, h);
}

cuif_widget* cuif_widget_create_knob(float x, float y, float w, float h, const char* label, float default_val, cuif_widget_value_changed_fn on_change) {
    cuif_widget* widget = cuif_widget_create_base(CUIF_WIDGET_KNOB, x, y, w, h);
    if (!widget) return NULL;
    widget->u.knob.value = default_val;
    widget->u.knob.default_value = default_val;
    strncpy(widget->u.knob.label, label, sizeof(widget->u.knob.label) - 1);
    widget->u.knob.on_change = on_change;
    return widget;
}

cuif_widget* cuif_widget_create_slider(float x, float y, float w, float h, bool is_vertical, cuif_widget_value_changed_fn on_change) {
    cuif_widget* widget = cuif_widget_create_base(CUIF_WIDGET_SLIDER, x, y, w, h);
    if (!widget) return NULL;
    widget->u.slider.value = 0.5f;
    widget->u.slider.is_vertical = is_vertical;
    widget->u.slider.on_change = on_change;
    return widget;
}

cuif_widget* cuif_widget_create_button(float x, float y, float w, float h, const char* label, bool is_toggle, void (*on_click)(cuif_widget* w)) {
    cuif_widget* widget = cuif_widget_create_base(CUIF_WIDGET_BUTTON, x, y, w, h);
    if (!widget) return NULL;
    strncpy(widget->u.button.label, label, sizeof(widget->u.button.label) - 1);
    widget->u.button.is_toggle = is_toggle;
    widget->u.button.state = false;
    widget->u.button.on_click = on_click;
    return widget;
}

cuif_widget* cuif_widget_create_dropdown(float x, float y, float w, float h, const char** items, int item_count, cuif_widget_value_changed_fn on_change) {
    cuif_widget* widget = cuif_widget_create_base(CUIF_WIDGET_DROPDOWN, x, y, w, h);
    if (!widget) return NULL;
    widget->u.dropdown.items = items;
    widget->u.dropdown.item_count = item_count;
    widget->u.dropdown.selected_index = 0;
    widget->u.dropdown.is_open = false;
    widget->u.dropdown.on_change = on_change;
    return widget;
}

cuif_widget* cuif_widget_create_bezier_editor(float x, float y, float w, float h, int num_nodes, void (*on_curve_change)(cuif_widget* w)) {
    cuif_widget* widget = cuif_widget_create_base(CUIF_WIDGET_BEZIER_EDITOR, x, y, w, h);
    if (!widget) return NULL;
    
    if (num_nodes > 8) num_nodes = 8;
    if (num_nodes < 2) num_nodes = 2;
    widget->u.bezier_editor.node_count = num_nodes;
    widget->u.bezier_editor.active_node = -1;
    widget->u.bezier_editor.on_curve_change = on_curve_change;
    
    /* Initialize nodes evenly distributed horizontally */
    for (int i = 0; i < num_nodes; ++i) {
        widget->u.bezier_editor.node_x[i] = (float)i / (num_nodes - 1);
        widget->u.bezier_editor.node_y[i] = 0.5f;
    }
    
    return widget;
}

cuif_widget* cuif_widget_create_analyzer(float x, float y, float w, float h, float* buffer, int buffer_size, cuif_color color) {
    cuif_widget* widget = cuif_widget_create_base(CUIF_WIDGET_ANALYZER, x, y, w, h);
    if (!widget) return NULL;
    widget->u.analyzer.buffer = buffer;
    widget->u.analyzer.buffer_size = buffer_size;
    widget->u.analyzer.line_color = color;
    return widget;
}

cuif_widget* cuif_widget_create_tabbar(float x, float y, float w, float h, const char** labels, int tab_count, cuif_widget_value_changed_fn on_change) {
    cuif_widget* widget = cuif_widget_create_base(CUIF_WIDGET_TABBAR, x, y, w, h);
    if (!widget) return NULL;
    widget->u.tabbar.tab_labels = labels;
    widget->u.tabbar.tab_count = tab_count;
    widget->u.tabbar.selected_index = 0;
    widget->u.tabbar.on_change = on_change;
    return widget;
}

/* ---- Meter, transfer curve, EQ curve (#131) ---- */

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

cuif_widget* cuif_widget_create_meter(float x, float y, float w, float h, float min_db, float max_db, bool is_vertical) {
    cuif_widget* widget = cuif_widget_create_base(CUIF_WIDGET_METER, x, y, w, h);
    if (!widget) return NULL;
    if (max_db <= min_db) max_db = min_db + 1.0f;
    widget->u.meter.min_db = min_db;
    widget->u.meter.max_db = max_db;
    widget->u.meter.is_vertical = is_vertical;
    widget->u.meter.fill_from_top = false;
    widget->u.meter.fed_db = min_db;
    widget->u.meter.value_db = min_db;
    widget->u.meter.peak_db = min_db;
    widget->u.meter.fall_db_per_second = 24.0f;
    widget->u.meter.peak_hold_ms = 1500.0f;
    widget->u.meter.peak_age_ms = 0.0f;
    widget->u.meter.last_step_ms = 0;
    return widget;
}

void cuif_widget_meter_feed(cuif_widget* w, float level_db) {
    if (!w || w->type != CUIF_WIDGET_METER) return;
    float v = clampf(level_db, w->u.meter.min_db, w->u.meter.max_db);
    w->u.meter.fed_db = v;
    if (v > w->u.meter.value_db) w->u.meter.value_db = v; /* rises instantly */
    if (v >= w->u.meter.peak_db) {
        w->u.meter.peak_db = v;
        w->u.meter.peak_age_ms = 0.0f;
    }
}

void cuif_widget_meter_step(cuif_widget* w, float dt_seconds) {
    if (!w || w->type != CUIF_WIDGET_METER || dt_seconds <= 0.0f) return;
    float fall = w->u.meter.fall_db_per_second * dt_seconds;
    float floor_db = w->u.meter.fed_db;
    if (w->u.meter.value_db > floor_db) {
        w->u.meter.value_db -= fall;
        if (w->u.meter.value_db < floor_db) w->u.meter.value_db = floor_db;
    }
    w->u.meter.peak_age_ms += dt_seconds * 1000.0f;
    if (w->u.meter.peak_age_ms > w->u.meter.peak_hold_ms && w->u.meter.peak_db > w->u.meter.value_db) {
        w->u.meter.peak_db -= fall;
        if (w->u.meter.peak_db < w->u.meter.value_db) w->u.meter.peak_db = w->u.meter.value_db;
    }
}

cuif_widget* cuif_widget_create_transfer_curve(float x, float y, float w, float h, float min_db, float max_db) {
    cuif_widget* widget = cuif_widget_create_base(CUIF_WIDGET_TRANSFER_CURVE, x, y, w, h);
    if (!widget) return NULL;
    if (max_db <= min_db) max_db = min_db + 1.0f;
    widget->u.transfer_curve.min_db = min_db;
    widget->u.transfer_curve.max_db = max_db;
    widget->u.transfer_curve.threshold_db = max_db; /* 1:1 until set */
    widget->u.transfer_curve.ratio = 1.0f;
    widget->u.transfer_curve.knee_db = 0.0f;
    widget->u.transfer_curve.range_db = 0.0f;
    widget->u.transfer_curve.expander = false;
    widget->u.transfer_curve.has_point = false;
    return widget;
}

void cuif_widget_transfer_curve_set(cuif_widget* w, float threshold_db, float ratio, float knee_db, float range_db, bool expander) {
    if (!w || w->type != CUIF_WIDGET_TRANSFER_CURVE) return;
    w->u.transfer_curve.threshold_db = threshold_db;
    w->u.transfer_curve.ratio = ratio < 1.0f ? 1.0f : ratio;
    w->u.transfer_curve.knee_db = knee_db < 0.0f ? 0.0f : knee_db;
    w->u.transfer_curve.range_db = range_db < 0.0f ? 0.0f : range_db;
    w->u.transfer_curve.expander = expander;
}

void cuif_widget_transfer_curve_feed(cuif_widget* w, float in_db, float out_db) {
    if (!w || w->type != CUIF_WIDGET_TRANSFER_CURVE) return;
    w->u.transfer_curve.point_in_db = in_db;
    w->u.transfer_curve.point_out_db = out_db;
    w->u.transfer_curve.has_point = true;
}

/* Mirrors loudio::GainComputer::outputDb (DSP/include/loudio/dsp/GainComputer.h). */
float cuif_widget_transfer_curve_output_db(const cuif_widget* w, float x) {
    if (!w || w->type != CUIF_WIDGET_TRANSFER_CURVE) return x;
    float t = w->u.transfer_curve.threshold_db;
    float r = w->u.transfer_curve.ratio;
    float k = w->u.transfer_curve.knee_db;
    float over = 2.0f * (x - t);
    if (!w->u.transfer_curve.expander) {
        float slope = 1.0f / r;
        if (over < -k) return x;
        if (over > k || k <= 0.0f) return t + (x - t) * slope;
        float d = x - t + k * 0.5f;
        return x + (slope - 1.0f) * d * d / (2.0f * k);
    } else {
        float y;
        if (over > k) y = x;
        else if (over < -k || k <= 0.0f) y = t + (x - t) * r;
        else { float d = x - t - k * 0.5f; y = x + (1.0f - r) * d * d / (2.0f * k); }
        float range = w->u.transfer_curve.range_db;
        if (range > 0.0f && x - y > range) y = x - range;
        return y;
    }
}

cuif_widget* cuif_widget_create_eq_curve(float x, float y, float w, float h, int band_count,
                                         float min_hz, float max_hz, float min_db, float max_db,
                                         cuif_widget_value_changed_fn on_band_change) {
    cuif_widget* widget = cuif_widget_create_base(CUIF_WIDGET_EQ_CURVE, x, y, w, h);
    if (!widget) return NULL;
    if (band_count < 1) band_count = 1;
    if (band_count > CUIF_EQ_CURVE_MAX_BANDS) band_count = CUIF_EQ_CURVE_MAX_BANDS;
    if (min_hz <= 0.0f) min_hz = 1.0f;
    if (max_hz <= min_hz) max_hz = min_hz * 2.0f;
    if (max_db <= min_db) max_db = min_db + 1.0f;
    widget->u.eq_curve.band_count = band_count;
    for (int i = 0; i < CUIF_EQ_CURVE_MAX_BANDS; ++i) {
        widget->u.eq_curve.band_hz[i] = 1000.0f;
        widget->u.eq_curve.band_gain_db[i] = 0.0f;
        widget->u.eq_curve.band_q[i] = 0.707f;
        widget->u.eq_curve.band_enabled[i] = false;
    }
    widget->u.eq_curve.min_hz = min_hz;
    widget->u.eq_curve.max_hz = max_hz;
    widget->u.eq_curve.min_db = min_db;
    widget->u.eq_curve.max_db = max_db;
    widget->u.eq_curve.min_q = 0.1f;
    widget->u.eq_curve.max_q = 30.0f;
    widget->u.eq_curve.response_db = NULL;
    widget->u.eq_curve.response_count = 0;
    widget->u.eq_curve.active_band = -1;
    widget->u.eq_curve.draw_background = true;
    widget->u.eq_curve.on_band_change = on_band_change;
    return widget;
}

void cuif_widget_eq_curve_set_band(cuif_widget* w, int band, float hz, float gain_db, float q, bool enabled) {
    if (!w || w->type != CUIF_WIDGET_EQ_CURVE || band < 0 || band >= w->u.eq_curve.band_count) return;
    w->u.eq_curve.band_hz[band] = clampf(hz, w->u.eq_curve.min_hz, w->u.eq_curve.max_hz);
    w->u.eq_curve.band_gain_db[band] = clampf(gain_db, w->u.eq_curve.min_db, w->u.eq_curve.max_db);
    w->u.eq_curve.band_q[band] = clampf(q, w->u.eq_curve.min_q, w->u.eq_curve.max_q);
    w->u.eq_curve.band_enabled[band] = enabled;
}

void cuif_widget_eq_curve_set_response(cuif_widget* w, const float* response_db, int count) {
    if (!w || w->type != CUIF_WIDGET_EQ_CURVE) return;
    w->u.eq_curve.response_db = response_db;
    w->u.eq_curve.response_count = response_db ? count : 0;
}

/* Log-frequency x and linear-dB y mapping shared by render, hit test and drag. */
static float eq_hz_to_x(const cuif_widget* w, float hz) {
    float lo = logf(w->u.eq_curve.min_hz), hi = logf(w->u.eq_curve.max_hz);
    float t = (logf(clampf(hz, w->u.eq_curve.min_hz, w->u.eq_curve.max_hz)) - lo) / (hi - lo);
    return w->x + t * w->w;
}
static float eq_x_to_hz(const cuif_widget* w, float x) {
    float t = clampf((x - w->x) / w->w, 0.0f, 1.0f);
    float lo = logf(w->u.eq_curve.min_hz), hi = logf(w->u.eq_curve.max_hz);
    return expf(lo + t * (hi - lo));
}
static float eq_db_to_y(const cuif_widget* w, float db) {
    float t = (clampf(db, w->u.eq_curve.min_db, w->u.eq_curve.max_db) - w->u.eq_curve.min_db) / (w->u.eq_curve.max_db - w->u.eq_curve.min_db);
    return w->y + w->h - t * w->h;
}
static float eq_y_to_db(const cuif_widget* w, float y) {
    float t = clampf((w->y + w->h - y) / w->h, 0.0f, 1.0f);
    return w->u.eq_curve.min_db + t * (w->u.eq_curve.max_db - w->u.eq_curve.min_db);
}

void cuif_widget_eq_curve_handle_position(const cuif_widget* w, int band, float* out_x, float* out_y) {
    if (!w || w->type != CUIF_WIDGET_EQ_CURVE || band < 0 || band >= w->u.eq_curve.band_count) {
        if (out_x) *out_x = 0.0f;
        if (out_y) *out_y = 0.0f;
        return;
    }
    if (out_x) *out_x = eq_hz_to_x(w, w->u.eq_curve.band_hz[band]);
    if (out_y) *out_y = eq_db_to_y(w, w->u.eq_curve.band_gain_db[band]);
}

/* Nearest enabled band handle within CUIF_EQ_HANDLE_RADIUS of (mx, my), or -1. */
static int eq_band_at(const cuif_widget* w, float mx, float my) {
    int best = -1;
    float best_d = CUIF_EQ_HANDLE_RADIUS;
    for (int i = 0; i < w->u.eq_curve.band_count; ++i) {
        if (!w->u.eq_curve.band_enabled[i]) continue;
        float hx, hy;
        cuif_widget_eq_curve_handle_position(w, i, &hx, &hy);
        float d = sqrtf((mx - hx) * (mx - hx) + (my - hy) * (my - hy));
        if (d <= best_d) { best_d = d; best = i; }
    }
    return best;
}

static void draw_graph_frame(const cuif_widget* w, const cuif_theme* theme, int cols, int rows, bool background) {
    if (background) cuif_draw_rect(w->x, w->y, w->w, w->h, theme->background, true);
    cuif_draw_rect(w->x, w->y, w->w, w->h, theme->border, false);
    for (int i = 1; i < cols; ++i) {
        float gx = w->x + (float)i / cols * w->w;
        cuif_draw_line(gx, w->y, gx, w->y + w->h, 1.0f, theme->grid_line);
    }
    for (int i = 1; i < rows; ++i) {
        float gy = w->y + (float)i / rows * w->h;
        cuif_draw_line(w->x, gy, w->x + w->w, gy, 1.0f, theme->grid_line);
    }
}

/* ---- Readout + keyboard (#131) ---- */

static void readout_format(const cuif_widget* w, char* out, size_t out_size) {
    char fmt[16];
    int d = w->u.readout.decimals;
    if (d < 0) d = 0;
    if (d > 6) d = 6;
    snprintf(fmt, sizeof(fmt), "%%.%df%%s", d);
    snprintf(out, out_size, fmt, w->u.readout.value, w->u.readout.unit);
}

cuif_widget* cuif_widget_create_readout(float x, float y, float w, float h, float min_value, float max_value,
                                        int decimals, const char* unit, cuif_widget_value_changed_fn on_commit) {
    cuif_widget* widget = cuif_widget_create_base(CUIF_WIDGET_READOUT, x, y, w, h);
    if (!widget) return NULL;
    if (max_value < min_value) { float t = min_value; min_value = max_value; max_value = t; }
    widget->u.readout.min_value = min_value;
    widget->u.readout.max_value = max_value;
    widget->u.readout.value = clampf(0.0f, min_value, max_value);
    widget->u.readout.decimals = decimals;
    if (unit) strncpy(widget->u.readout.unit, unit, sizeof(widget->u.readout.unit) - 1);
    widget->u.readout.editing = false;
    widget->u.readout.text[0] = '\0';
    widget->u.readout.text_len = 0;
    widget->u.readout.on_commit = on_commit;
    return widget;
}

void cuif_widget_readout_set_value(cuif_widget* w, float value) {
    if (!w || w->type != CUIF_WIDGET_READOUT || w->u.readout.editing) return;
    w->u.readout.value = clampf(value, w->u.readout.min_value, w->u.readout.max_value);
}

void cuif_widget_readout_text(const cuif_widget* w, char* out, size_t out_size) {
    if (!w || !out || out_size == 0) return;
    if (w->type != CUIF_WIDGET_READOUT) { out[0] = '\0'; return; }
    if (w->u.readout.editing) {
        strncpy(out, w->u.readout.text, out_size - 1);
        out[out_size - 1] = '\0';
    } else {
        readout_format(w, out, out_size);
    }
}

static void readout_begin_edit(cuif_widget* w) {
    char fmt[16];
    int d = w->u.readout.decimals < 0 ? 0 : (w->u.readout.decimals > 6 ? 6 : w->u.readout.decimals);
    snprintf(fmt, sizeof(fmt), "%%.%df", d);
    snprintf(w->u.readout.text, sizeof(w->u.readout.text), fmt, w->u.readout.value);
    w->u.readout.text_len = (int)strlen(w->u.readout.text);
    w->u.readout.editing = true;
    if (cuif_current_window) cuif_window_set_focused_widget(cuif_current_window, w);
}

static void readout_end_edit(cuif_widget* w, bool commit) {
    if (!w->u.readout.editing) return;
    w->u.readout.editing = false;
    if (commit && w->u.readout.text_len > 0) {
        char* end = NULL;
        float v = strtof(w->u.readout.text, &end);
        if (end != w->u.readout.text) {
            v = clampf(v, w->u.readout.min_value, w->u.readout.max_value);
            w->u.readout.value = v;
            if (w->u.readout.on_commit) w->u.readout.on_commit(w, v);
        }
    }
    w->u.readout.text[0] = '\0';
    w->u.readout.text_len = 0;
    if (cuif_current_window && cuif_window_get_focused_widget(cuif_current_window) == w) {
        cuif_window_set_focused_widget(cuif_current_window, NULL);
    }
}

void cuif_widget_cancel_text_entry(void) {
    if (!cuif_current_window) return;
    cuif_widget* f = cuif_window_get_focused_widget(cuif_current_window);
    if (f && f->type == CUIF_WIDGET_READOUT) readout_end_edit(f, false);
    cuif_window_set_focused_widget(cuif_current_window, NULL);
}

bool cuif_widget_dispatch_key(cuif_widget* root, int type, uint32_t key, uint32_t modifiers) {
    (void)root;
    (void)modifiers;
    if (!cuif_current_window) return false;
    cuif_widget* w = cuif_window_get_focused_widget(cuif_current_window);
    if (!w || w->type != CUIF_WIDGET_READOUT || !w->u.readout.editing) return false;

    if (type == CUIF_KEY_DOWN) {
        switch (key) {
            case CUIF_VK_ENTER:  readout_end_edit(w, true);  return true;
            case CUIF_VK_ESCAPE: readout_end_edit(w, false); return true;
            case CUIF_VK_BACKSPACE:
                if (w->u.readout.text_len > 0) w->u.readout.text[--w->u.readout.text_len] = '\0';
                return true;
            default:
                return true; /* other keys are swallowed while editing */
        }
    }
    if (type == CUIF_KEY_CHAR) {
        char c = 0;
        if (key >= '0' && key <= '9') c = (char)key;
        else if (key == '-' || key == '+' || key == '.') c = (char)key;
        else if (key == ',') c = '.';
        if (c == 0) return true; /* rejected character */
        if (w->u.readout.text_len < CUIF_READOUT_TEXT_MAX - 1) {
            w->u.readout.text[w->u.readout.text_len++] = c;
            w->u.readout.text[w->u.readout.text_len] = '\0';
        }
        return true;
    }
    return type == CUIF_KEY_UP;
}

/* Double-click detection state (see widget.h). One UI thread, so plain statics. */
static cuif_widget* s_last_click_widget = NULL;
static unsigned long long s_last_click_time_ms = 0;
static float s_last_click_x = 0.0f, s_last_click_y = 0.0f;
static unsigned s_double_click_time_ms = 0; /* 0 = system setting */

void cuif_widget_set_double_click_time_ms(unsigned ms) {
    s_double_click_time_ms = ms;
}

static unsigned long long double_click_window_ms(void) {
    return s_double_click_time_ms ? s_double_click_time_ms : (unsigned long long)GetDoubleClickTime();
}

/* Returns true if this down completes a double-click on `hit`, and records
 * the click either way. A completed double-click clears the record so a
 * third click starts over rather than chaining. */
static bool register_click(cuif_widget* hit, float mx, float my) {
    unsigned long long now = GetTickCount64();
    bool is_double = hit == s_last_click_widget &&
                     now - s_last_click_time_ms <= double_click_window_ms() &&
                     fabsf(mx - s_last_click_x) <= 4.0f && fabsf(my - s_last_click_y) <= 4.0f;
    s_last_click_widget = is_double ? NULL : hit;
    s_last_click_time_ms = now;
    s_last_click_x = mx;
    s_last_click_y = my;
    return is_double;
}

void cuif_widget_destroy(cuif_widget* w) {
    if (!w) return;
    if (cuif_current_window && cuif_window_get_focused_widget(cuif_current_window) == w) {
        cuif_window_set_focused_widget(cuif_current_window, NULL);
    }
    for (int i = 0; i < w->child_count; ++i) {
        cuif_widget_destroy(w->children[i]);
    }
    free(w->children);
    if (s_last_click_widget == w) s_last_click_widget = NULL;
    if (cuif_current_window) {
        if (cuif_window_get_active_widget(cuif_current_window) == w) cuif_window_set_active_widget(cuif_current_window, NULL);
        if (cuif_window_get_hovered_widget(cuif_current_window) == w) cuif_window_set_hovered_widget(cuif_current_window, NULL);
        if (cuif_window_get_open_dropdown(cuif_current_window) == w) cuif_window_set_open_dropdown(cuif_current_window, NULL);
    }
    free(w);
}

void cuif_widget_add_child(cuif_widget* parent, cuif_widget* child) {
    if (!parent || !child) return;
    if (parent->child_count >= parent->child_capacity) {
        parent->child_capacity = parent->child_capacity == 0 ? 4 : parent->child_capacity * 2;
        parent->children = (cuif_widget**)realloc(parent->children, parent->child_capacity * sizeof(cuif_widget*));
    }
    parent->children[parent->child_count++] = child;
    child->parent = parent;
}

/* Helper to check if point is inside widget bounds */
static bool point_in_widget(cuif_widget* w, float px, float py) {
    return px >= w->x && px <= w->x + w->w && py >= w->y && py <= w->y + w->h;
}

/* Recursive hit-testing */
static cuif_widget* hit_test(cuif_widget* w, float mx, float my) {
    if (!w->visible || !w->enabled) return NULL;
    if (!point_in_widget(w, mx, my)) return NULL;

    /* Check children in reverse order (front-most first) */
    for (int i = w->child_count - 1; i >= 0; --i) {
        cuif_widget* hit = hit_test(w->children[i], mx, my);
        if (hit) return hit;
    }

    return w;
}

/* Draw a circular arc */
static void draw_arc(float cx, float cy, float r, float start_angle, float end_angle, float thickness, cuif_color color) {
    float points[CUIF_MAX_ARC_POINTS * 2];
    int point_count = cuif_generate_arc_points(cx, cy, r, start_angle, end_angle, points, CUIF_MAX_ARC_POINTS);
    cuif_draw_polyline(points, point_count, thickness, color, false);
}

/* Catmull-Rom spline interpolation helper */
static float evaluate_catmull_rom(float p0, float p1, float p2, float p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) +
                   (-p0 + p2) * t +
                   (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

static void draw_dropdown_overlay(cuif_widget* w) {
    if (!w || w->type != CUIF_WIDGET_DROPDOWN || !w->u.dropdown.is_open) return;
    float item_h = 24.0f;
    float total_h = w->u.dropdown.item_count * item_h;
    
    const cuif_theme* active_theme = cuif_get_theme();
    cuif_color bg = active_theme->dropdown_overlay_bg;
    cuif_color border = active_theme->dropdown_overlay_border;
    
    /* Draw shadow/glow rectangle */
    cuif_draw_rect(w->x - 2.0f, w->y + w->h, w->w + 4.0f, total_h + 2.0f, cuif_rgba(0.0f, 0.0f, 0.0f, 0.3f), true);
    /* Draw background */
    cuif_draw_rect(w->x, w->y + w->h, w->w, total_h, bg, true);
    /* Draw border */
    cuif_draw_rect(w->x, w->y + w->h, w->w, total_h, border, false);
    
    for (int i = 0; i < w->u.dropdown.item_count; ++i) {
        float iy = w->y + w->h + i * item_h;
        
        /* Check hover in overlay */
        POINT pt;
        GetCursorPos(&pt);
        HWND hwnd = WindowFromPoint(pt);
        if (hwnd) {
            ScreenToClient(hwnd, &pt);
            float mx = (float)pt.x;
            float my = (float)pt.y;
            if (mx >= w->x && mx <= w->x + w->w && my >= iy && my < iy + item_h) {
                cuif_draw_rect(w->x, iy, w->w, item_h, active_theme->dropdown_hover_bg, true);
            }
        }

        if (cuif_global_font) {
            cuif_color text_col = active_theme->text_secondary;
            if (i == w->u.dropdown.selected_index) {
                text_col = active_theme->primary;
            }
            cuif_draw_text(cuif_global_font, w->u.dropdown.items[i], w->x + 8.0f, iy + 16.0f, text_col);
        }
    }
}

void cuif_widget_render(cuif_widget* w) {
    if (!w || !w->visible) return;

    const cuif_theme* active_theme = cuif_get_theme();
    cuif_color theme_primary = active_theme->primary;
    cuif_color theme_bg_dark = active_theme->background;
    cuif_color theme_panel_bg = active_theme->panel_bg;
    cuif_color theme_border = active_theme->border;
    cuif_color text_white = active_theme->text_primary;
    cuif_color text_gray = active_theme->text_secondary;

    switch (w->type) {
        case CUIF_WIDGET_CONTAINER:
            break;

        case CUIF_WIDGET_KNOB: {
            float cx = w->x + w->w * 0.5f;
            float cy = w->y + w->h * 0.4f;
            float r = w->w * 0.32f;
            
            /* Draw outer knob track */
            draw_arc(cx, cy, r, 0.75f * (float)M_PI, 2.25f * (float)M_PI, 4.0f, theme_border);
            
            /* Draw active value arc */
            float active_angle = 0.75f * (float)M_PI + w->u.knob.value * 1.5f * (float)M_PI;
            draw_arc(cx, cy, r, 0.75f * (float)M_PI, active_angle, 4.5f, theme_primary);
            
            /* Draw knob cap */
            cuif_draw_rounded_rect(cx - r + 3, cy - r + 3, (r - 3) * 2, (r - 3) * 2, r - 3, theme_panel_bg, true);
            cuif_draw_rounded_rect(cx - r + 3, cy - r + 3, (r - 3) * 2, (r - 3) * 2, r - 3, theme_border, false);
            
            /* Draw pointer line */
            float px = cx + (r - 5.0f) * cosf(active_angle);
            float py = cy + (r - 5.0f) * sinf(active_angle);
            cuif_draw_line(cx + 2.0f * cosf(active_angle), cy + 2.0f * sinf(active_angle), px, py, 2.5f, text_white);

            /* Draw label and value text */
            if (cuif_global_font) {
                float tx = cx - cuif_font_measure_text(cuif_global_font, w->u.knob.label) * 0.5f;
                cuif_draw_text(cuif_global_font, w->u.knob.label, tx, w->y + w->h - 18.0f, text_gray);

                char val_str[16];
                sprintf_s(val_str, sizeof(val_str), "%.2f", w->u.knob.value);
                float vx = cx - cuif_font_measure_text(cuif_global_font, val_str) * 0.5f;
                cuif_draw_text(cuif_global_font, val_str, vx, w->y + w->h - 4.0f, text_white);
            }
            break;
        }

        case CUIF_WIDGET_SLIDER: {
            bool vert = w->u.slider.is_vertical;
            cuif_draw_rounded_rect(w->x, w->y, w->w, w->h, 4.0f, theme_panel_bg, true);
            cuif_draw_rounded_rect(w->x, w->y, w->w, w->h, 4.0f, theme_border, false);
            
            if (vert) {
                float track_x = w->x + w->w * 0.5f;
                float fill_h = w->u.slider.value * (w->h - 12.0f);
                /* Background track line */
                cuif_draw_line(track_x, w->y + 6.0f, track_x, w->y + w->h - 6.0f, 4.0f, theme_border);
                /* Active fill line */
                cuif_draw_line(track_x, w->y + w->h - 6.0f - fill_h, track_x, w->y + w->h - 6.0f, 4.5f, theme_primary);
                /* Handle thumb */
                float thumb_y = w->y + w->h - 6.0f - fill_h - 6.0f;
                cuif_draw_rounded_rect(w->x + 3.0f, thumb_y, w->w - 6.0f, 12.0f, 3.0f, text_white, true);
            } else {
                float track_y = w->y + w->h * 0.5f;
                float fill_w = w->u.slider.value * (w->w - 12.0f);
                /* Background track line */
                cuif_draw_line(w->x + 6.0f, track_y, w->x + w->w - 6.0f, track_y, 4.0f, theme_border);
                /* Active fill line */
                cuif_draw_line(w->x + 6.0f, track_y, w->x + 6.0f + fill_w, track_y, 4.5f, theme_primary);
                /* Handle thumb */
                float thumb_x = w->x + 6.0f + fill_w - 6.0f;
                cuif_draw_rounded_rect(thumb_x, w->y + 3.0f, 12.0f, w->h - 6.0f, 3.0f, text_white, true);
            }
            break;
        }

        case CUIF_WIDGET_BUTTON: {
            cuif_color btn_bg = w->u.button.state ? theme_primary : theme_panel_bg;
            cuif_draw_rounded_rect(w->x, w->y, w->w, w->h, 4.0f, btn_bg, true);
            cuif_draw_rounded_rect(w->x, w->y, w->w, w->h, 4.0f, theme_border, false);
            
            if (cuif_global_font) {
                float tx = w->x + (w->w - cuif_font_measure_text(cuif_global_font, w->u.button.label)) * 0.5f;
                float ty = w->y + (w->h + 10.0f) * 0.5f;
                cuif_draw_text(cuif_global_font, w->u.button.label, tx, ty, text_white);
            }
            break;
        }

        case CUIF_WIDGET_DROPDOWN: {
            cuif_draw_rounded_rect(w->x, w->y, w->w, w->h, 4.0f, theme_panel_bg, true);
            cuif_draw_rounded_rect(w->x, w->y, w->w, w->h, 4.0f, theme_border, false);
            
            /* Draw drop arrow */
            float ax = w->x + w->w - 16.0f;
            float ay = w->y + w->h * 0.5f;
            glColor4f(text_gray.r, text_gray.g, text_gray.b, text_gray.a);
            glBegin(GL_TRIANGLES);
            glVertex2f(ax - 4, ay - 2);
            glVertex2f(ax + 4, ay - 2);
            glVertex2f(ax, ay + 3);
            glEnd();
            
            if (cuif_global_font && w->u.dropdown.selected_index < w->u.dropdown.item_count) {
                const char* item_text = w->u.dropdown.items[w->u.dropdown.selected_index];
                cuif_draw_text(cuif_global_font, item_text, w->x + 8.0f, w->y + w->h * 0.5f + 5.0f, text_white);
            }
            break;
        }

        case CUIF_WIDGET_BEZIER_EDITOR: {
            /* Draw dark grid background */
            cuif_draw_rect(w->x, w->y, w->w, w->h, theme_bg_dark, true);
            cuif_draw_rect(w->x, w->y, w->w, w->h, theme_border, false);
            
            /* Draw grid lines */
            int grid_cols = 10;
            int grid_rows = 6;
            cuif_color grid_color = active_theme->grid_line;
            for (int i = 1; i < grid_cols; ++i) {
                float gx = w->x + (float)i / grid_cols * w->w;
                cuif_draw_line(gx, w->y, gx, w->y + w->h, 1.0f, grid_color);
            }
            for (int i = 1; i < grid_rows; ++i) {
                float gy = w->y + (float)i / grid_rows * w->h;
                cuif_draw_line(w->x, gy, w->x + w->w, gy, 1.0f, grid_color);
            }
            
            int count = w->u.bezier_editor.node_count;
            float* xs = w->u.bezier_editor.node_x;
            float* ys = w->u.bezier_editor.node_y;

            /* We will draw a filled gradient area beneath the curve for visual flare! */
            glBegin(GL_QUAD_STRIP);
            int segments_per_segment = 24;
            for (int i = 0; i < count - 1; ++i) {
                float p0x = (i == 0) ? (2.0f * xs[0] - xs[1]) : xs[i - 1];
                float p0y = (i == 0) ? (2.0f * ys[0] - ys[1]) : ys[i - 1];
                float p1x = xs[i];
                float p1y = ys[i];
                float p2x = xs[i + 1];
                float p2y = ys[i + 1];
                float p3x = (i + 2 >= count) ? (2.0f * xs[count - 1] - xs[count - 2]) : xs[i + 2];
                float p3y = (i + 2 >= count) ? (2.0f * ys[count - 1] - ys[count - 2]) : ys[i + 2];
                
                for (int j = 0; j <= segments_per_segment; ++j) {
                    float t = (float)j / segments_per_segment;
                    float cx = evaluate_catmull_rom(p0x, p1x, p2x, p3x, t);
                    float cy = evaluate_catmull_rom(p0y, p1y, p2y, p3y, t);
                    
                    float world_x = w->x + cx * w->w;
                    float world_y = w->y + cy * w->h;
                    
                    /* Top vertex (on curve) */
                    glColor4f(theme_primary.r, theme_primary.g, theme_primary.b, 0.25f);
                    glVertex2f(world_x, world_y);
                    
                    /* Bottom vertex (fixed at bottom edge) */
                    glColor4f(theme_primary.r, theme_primary.g, theme_primary.b, 0.01f);
                    glVertex2f(world_x, w->y + w->h);
                }
            }
            glEnd();
            
            /* Draw the solid curve stroke */
            {
                /* Worst case: 7 catmull-rom segments (node_x/node_y cap at 8 nodes) * 25 samples each. */
                float curve_points[175 * 2];
                int curve_point_count = 0;

                for (int i = 0; i < count - 1; ++i) {
                    float p0x = (i == 0) ? (2.0f * xs[0] - xs[1]) : xs[i - 1];
                    float p0y = (i == 0) ? (2.0f * ys[0] - ys[1]) : ys[i - 1];
                    float p1x = xs[i];
                    float p1y = ys[i];
                    float p2x = xs[i + 1];
                    float p2y = ys[i + 1];
                    float p3x = (i + 2 >= count) ? (2.0f * xs[count - 1] - xs[count - 2]) : xs[i + 2];
                    float p3y = (i + 2 >= count) ? (2.0f * ys[count - 1] - ys[count - 2]) : ys[i + 2];

                    for (int j = 0; j <= segments_per_segment; ++j) {
                        float t = (float)j / segments_per_segment;
                        float cx = evaluate_catmull_rom(p0x, p1x, p2x, p3x, t);
                        float cy = evaluate_catmull_rom(p0y, p1y, p2y, p3y, t);
                        if (curve_point_count < 175) {
                            curve_points[curve_point_count * 2] = w->x + cx * w->w;
                            curve_points[curve_point_count * 2 + 1] = w->y + cy * w->h;
                            curve_point_count++;
                        }
                    }
                }

                cuif_draw_polyline(curve_points, curve_point_count, 3.0f, cuif_rgba(theme_primary.r, theme_primary.g, theme_primary.b, 1.0f), false);
            }
            
            /* Draw nodes */
            for (int i = 0; i < count; ++i) {
                float nx = w->x + xs[i] * w->w;
                float ny = w->y + ys[i] * w->h;
                cuif_color node_color = (i == w->u.bezier_editor.active_node) ? text_white : theme_primary;
                cuif_draw_rounded_rect(nx - 5.0f, ny - 5.0f, 10.0f, 10.0f, 5.0f, node_color, true);
                cuif_draw_rounded_rect(nx - 5.0f, ny - 5.0f, 10.0f, 10.0f, 5.0f, text_white, false);
            }
            break;
        }

        case CUIF_WIDGET_TABBAR: {
            int count = w->u.tabbar.tab_count;
            if (count <= 0) break;
            float tab_w = w->w / (float)count;

            for (int i = 0; i < count; ++i) {
                float tx = w->x + i * tab_w;
                bool selected = (i == w->u.tabbar.selected_index);
                cuif_color tab_bg = selected ? theme_panel_bg : theme_bg_dark;
                cuif_draw_rect(tx, w->y, tab_w, w->h, tab_bg, true);
                cuif_draw_rect(tx, w->y, tab_w, w->h, theme_border, false);

                if (selected) {
                    cuif_draw_rect(tx, w->y + w->h - 2.0f, tab_w, 2.0f, theme_primary, true);
                }

                if (cuif_global_font && w->u.tabbar.tab_labels && w->u.tabbar.tab_labels[i]) {
                    float lx = tx + (tab_w - cuif_font_measure_text(cuif_global_font, w->u.tabbar.tab_labels[i])) * 0.5f;
                    float ly = w->y + (w->h + 10.0f) * 0.5f;
                    cuif_draw_text(cuif_global_font, w->u.tabbar.tab_labels[i], lx, ly, selected ? text_white : text_gray);
                }
            }
            break;
        }

        case CUIF_WIDGET_ANALYZER: {
            /* Draw dark grid background */
            cuif_draw_rect(w->x, w->y, w->w, w->h, theme_bg_dark, true);
            cuif_draw_rect(w->x, w->y, w->w, w->h, theme_border, false);
            
            /* Draw grid lines */
            int grid_cols = 10;
            int grid_rows = 6;
            cuif_color grid_color = active_theme->grid_line;
            for (int i = 1; i < grid_cols; ++i) {
                float gx = w->x + (float)i / grid_cols * w->w;
                cuif_draw_line(gx, w->y, gx, w->y + w->h, 1.0f, grid_color);
            }
            for (int i = 1; i < grid_rows; ++i) {
                float gy = w->y + (float)i / grid_rows * w->h;
                cuif_draw_line(w->x, gy, w->x + w->w, gy, 1.0f, grid_color);
            }
            
            float* buf = w->u.analyzer.buffer;
            int size = w->u.analyzer.buffer_size;
            cuif_color col = w->u.analyzer.line_color;
            
            if (buf && size > 1) {
                /* Render filled path underneath */
                glBegin(GL_QUAD_STRIP);
                for (int i = 0; i < size; ++i) {
                    float bx = w->x + (float)i / (size - 1) * w->w;
                    float by = w->y + w->h - buf[i] * w->h;
                    if (by < w->y) by = w->y;
                    if (by > w->y + w->h) by = w->y + w->h;
                    
                    glColor4f(col.r, col.g, col.b, 0.15f);
                    glVertex2f(bx, by);
                    glColor4f(col.r, col.g, col.b, 0.01f);
                    glVertex2f(bx, w->y + w->h);
                }
                glEnd();

                /* Render solid stroke path */
                {
                    /* buffer_size is 64 in practice (see PluginEditor.cpp); generous headroom for any future FFT resolution. */
                    float trace_points[512 * 2];
                    int trace_point_count = size < 512 ? size : 512;

                    for (int i = 0; i < trace_point_count; ++i) {
                        float bx = w->x + (float)i / (size - 1) * w->w;
                        float by = w->y + w->h - buf[i] * w->h;
                        if (by < w->y) by = w->y;
                        if (by > w->y + w->h) by = w->y + w->h;
                        trace_points[i * 2] = bx;
                        trace_points[i * 2 + 1] = by;
                    }

                    cuif_draw_polyline(trace_points, trace_point_count, 2.0f, cuif_rgba(col.r, col.g, col.b, 1.0f), false);
                }
            }
            break;
        }

        case CUIF_WIDGET_METER: {
            /* Advance ballistics from the wall clock. */
            unsigned long long now = GetTickCount64();
            if (w->u.meter.last_step_ms != 0 && now > w->u.meter.last_step_ms) {
                cuif_widget_meter_step(w, (float)(now - w->u.meter.last_step_ms) * 0.001f);
            }
            w->u.meter.last_step_ms = now;

            cuif_draw_rounded_rect(w->x, w->y, w->w, w->h, 3.0f, theme_bg_dark, true);
            cuif_draw_rounded_rect(w->x, w->y, w->w, w->h, 3.0f, theme_border, false);

            float range = w->u.meter.max_db - w->u.meter.min_db;
            float t_val = (w->u.meter.value_db - w->u.meter.min_db) / range;
            float t_peak = (w->u.meter.peak_db - w->u.meter.min_db) / range;
            if (w->u.meter.fill_from_top) { t_val = 1.0f - t_val; t_peak = 1.0f - t_peak; }
            float inset = 2.0f;
            if (w->u.meter.is_vertical) {
                float inner_h = w->h - 2.0f * inset;
                float bar_h = t_val * inner_h;
                float bar_y = w->u.meter.fill_from_top ? w->y + inset : w->y + w->h - inset - bar_h;
                cuif_draw_rect(w->x + inset, bar_y, w->w - 2.0f * inset, bar_h, theme_primary, true);
                float py = w->u.meter.fill_from_top ? w->y + inset + t_peak * inner_h : w->y + w->h - inset - t_peak * inner_h;
                cuif_draw_line(w->x + inset, py, w->x + w->w - inset, py, 2.0f, text_white);
            } else {
                float inner_w = w->w - 2.0f * inset;
                float bar_w = t_val * inner_w;
                float bar_x = w->u.meter.fill_from_top ? w->x + w->w - inset - bar_w : w->x + inset;
                cuif_draw_rect(bar_x, w->y + inset, bar_w, w->h - 2.0f * inset, theme_primary, true);
                float px = w->u.meter.fill_from_top ? w->x + w->w - inset - t_peak * inner_w : w->x + inset + t_peak * inner_w;
                cuif_draw_line(px, w->y + inset, px, w->y + w->h - inset, 2.0f, text_white);
            }
            break;
        }

        case CUIF_WIDGET_TRANSFER_CURVE: {
            draw_graph_frame(w, active_theme, 6, 6, true);
            float lo = w->u.transfer_curve.min_db, hi = w->u.transfer_curve.max_db;
            /* Unity reference. */
            cuif_draw_line(w->x, w->y + w->h, w->x + w->w, w->y, 1.0f, text_gray);
            /* The curve, left to right. */
            enum { kPts = 96 };
            float pts[kPts * 2];
            for (int i = 0; i < kPts; ++i) {
                float t = (float)i / (kPts - 1);
                float in_db = lo + t * (hi - lo);
                float out_db = clampf(cuif_widget_transfer_curve_output_db(w, in_db), lo, hi);
                pts[i * 2] = w->x + t * w->w;
                pts[i * 2 + 1] = w->y + w->h - (out_db - lo) / (hi - lo) * w->h;
            }
            cuif_draw_polyline(pts, kPts, 2.0f, theme_primary, false);
            /* Threshold marker. */
            float tt = clampf((w->u.transfer_curve.threshold_db - lo) / (hi - lo), 0.0f, 1.0f);
            cuif_draw_line(w->x + tt * w->w, w->y, w->x + tt * w->w, w->y + w->h, 1.0f, active_theme->grid_line);
            if (w->u.transfer_curve.has_point) {
                float px = w->x + clampf((w->u.transfer_curve.point_in_db - lo) / (hi - lo), 0.0f, 1.0f) * w->w;
                float py = w->y + w->h - clampf((w->u.transfer_curve.point_out_db - lo) / (hi - lo), 0.0f, 1.0f) * w->h;
                cuif_draw_rounded_rect(px - 4.0f, py - 4.0f, 8.0f, 8.0f, 4.0f, text_white, true);
            }
            break;
        }

        case CUIF_WIDGET_EQ_CURVE: {
            draw_graph_frame(w, active_theme, 10, 6, w->u.eq_curve.draw_background);
            /* 0 dB line. */
            float zy = eq_db_to_y(w, 0.0f);
            cuif_draw_line(w->x, zy, w->x + w->w, zy, 1.0f, text_gray);
            /* Host-supplied response. */
            if (w->u.eq_curve.response_db && w->u.eq_curve.response_count > 1) {
                float pts[512 * 2];
                int n = w->u.eq_curve.response_count < 512 ? w->u.eq_curve.response_count : 512;
                for (int i = 0; i < n; ++i) {
                    pts[i * 2] = w->x + (float)i / (n - 1) * w->w;
                    pts[i * 2 + 1] = eq_db_to_y(w, w->u.eq_curve.response_db[i]);
                }
                cuif_draw_polyline(pts, n, 2.0f, theme_primary, false);
            }
            /* Handles: a filled dot per enabled band, the active one larger. */
            for (int i = 0; i < w->u.eq_curve.band_count; ++i) {
                if (!w->u.eq_curve.band_enabled[i]) continue;
                float hx, hy;
                cuif_widget_eq_curve_handle_position(w, i, &hx, &hy);
                float r = (i == w->u.eq_curve.active_band) ? 7.0f : 5.0f;
                cuif_draw_rounded_rect(hx - r, hy - r, 2.0f * r, 2.0f * r, r, theme_panel_bg, true);
                cuif_draw_rounded_rect(hx - r, hy - r, 2.0f * r, 2.0f * r, r, text_white, false);
                if (cuif_global_font) {
                    char label[4];
                    sprintf_s(label, sizeof(label), "%d", i + 1);
                    cuif_draw_text(cuif_global_font, label, hx + r + 2.0f, hy + 4.0f, text_white);
                }
            }
            break;
        }

        case CUIF_WIDGET_READOUT: {
            bool editing = w->u.readout.editing;
            cuif_draw_rounded_rect(w->x, w->y, w->w, w->h, 3.0f, editing ? theme_bg_dark : theme_panel_bg, true);
            cuif_draw_rounded_rect(w->x, w->y, w->w, w->h, 3.0f, editing ? theme_primary : theme_border, false);
            if (cuif_global_font) {
                char text[CUIF_READOUT_TEXT_MAX + 8];
                cuif_widget_readout_text(w, text, sizeof(text));
                if (editing) strncat(text, "_", sizeof(text) - strlen(text) - 1); /* caret */
                float tx = w->x + w->w * 0.5f - cuif_font_measure_text(cuif_global_font, text) * 0.5f;
                cuif_draw_text(cuif_global_font, text, tx, w->y + w->h * 0.5f + 5.0f, text_white);
            }
            break;
        }
    }

    /* Render children */
    cuif_widget* open_dropdown = cuif_current_window ? cuif_window_get_open_dropdown(cuif_current_window) : NULL;
    for (int i = 0; i < w->child_count; ++i) {
        if (w->children[i]->visible && w->children[i] != open_dropdown) {
            cuif_widget_render(w->children[i]);
        }
    }

    /* If we are the root window view container, draw open dropdown overlay last */
    if (!w->parent && open_dropdown) {
        draw_dropdown_overlay(open_dropdown);
    }
}

void cuif_widget_dispatch_mouse_down(cuif_widget* root, float mx, float my, int button) {
    if (!cuif_current_window) return;

    /* Close dropdown if we click elsewhere */
    cuif_widget* open_dropdown = cuif_window_get_open_dropdown(cuif_current_window);
    if (open_dropdown) {
        float item_h = 24.0f;
        float overlay_h = open_dropdown->u.dropdown.item_count * item_h;
        bool in_overlay = mx >= open_dropdown->x && mx <= open_dropdown->x + open_dropdown->w &&
                          my >= open_dropdown->y + open_dropdown->h && my <= open_dropdown->y + open_dropdown->h + overlay_h;
        
        if (in_overlay) {
            int idx = (int)((my - (open_dropdown->y + open_dropdown->h)) / item_h);
            if (idx >= 0 && idx < open_dropdown->u.dropdown.item_count) {
                open_dropdown->u.dropdown.selected_index = idx;
                if (open_dropdown->u.dropdown.on_change) {
                    open_dropdown->u.dropdown.on_change(open_dropdown, (float)idx);
                }
            }
            open_dropdown->u.dropdown.is_open = false;
            cuif_window_set_open_dropdown(cuif_current_window, NULL);
            return;
        } else {
            open_dropdown->u.dropdown.is_open = false;
            cuif_window_set_open_dropdown(cuif_current_window, NULL);
        }
    }

    cuif_widget* hit = hit_test(root, mx, my);

    /* A click anywhere but the readout being edited cancels its entry. */
    {
        cuif_widget* focused = cuif_window_get_focused_widget(cuif_current_window);
        if (focused && focused != hit) cuif_widget_cancel_text_entry();
    }
    if (!hit) return;

    cuif_window_set_active_widget(cuif_current_window, hit);

    bool double_click = register_click(hit, mx, my);

    switch (hit->type) {
        case CUIF_WIDGET_KNOB:
            if (double_click) {
                /* Reset to default; no drag, so the following mouse-moves
                 * don't immediately pull it away from the default again.
                 * Also drop it as the active widget so move/up ignore it. */
                hit->u.knob.dragging = false;
                cuif_window_set_active_widget(cuif_current_window, NULL);
                hit->u.knob.value = hit->u.knob.default_value;
                if (hit->u.knob.on_change) hit->u.knob.on_change(hit, hit->u.knob.value);
                break;
            }
            hit->u.knob.dragging = true;
            hit->u.knob.drag_start_val = hit->u.knob.value;
            hit->u.knob.drag_start_y = my;
            break;

        case CUIF_WIDGET_SLIDER:
            hit->u.slider.dragging = true;
            hit->u.slider.drag_start_val = hit->u.slider.value;
            if (hit->u.slider.is_vertical) {
                hit->u.slider.drag_start_pos = my;
                float val = 1.0f - (my - hit->y - 6.0f) / (hit->h - 12.0f);
                if (val < 0.0f) val = 0.0f;
                if (val > 1.0f) val = 1.0f;
                hit->u.slider.value = val;
                if (hit->u.slider.on_change) hit->u.slider.on_change(hit, val);
            } else {
                hit->u.slider.drag_start_pos = mx;
                float val = (mx - hit->x - 6.0f) / (hit->w - 12.0f);
                if (val < 0.0f) val = 0.0f;
                if (val > 1.0f) val = 1.0f;
                hit->u.slider.value = val;
                if (hit->u.slider.on_change) hit->u.slider.on_change(hit, val);
            }
            break;

        case CUIF_WIDGET_BUTTON:
            if (hit->u.button.is_toggle) {
                hit->u.button.state = !hit->u.button.state;
            } else {
                hit->u.button.state = true;
            }
            if (hit->u.button.on_click) {
                hit->u.button.on_click(hit);
            }
            break;

        case CUIF_WIDGET_DROPDOWN:
            hit->u.dropdown.is_open = true;
            cuif_window_set_open_dropdown(cuif_current_window, hit);
            break;

        case CUIF_WIDGET_TABBAR: {
            int count = hit->u.tabbar.tab_count;
            if (count <= 0) break;
            float tab_w = hit->w / (float)count;
            int idx = (int)((mx - hit->x) / tab_w);
            if (idx < 0) idx = 0;
            if (idx >= count) idx = count - 1;

            if (idx != hit->u.tabbar.selected_index) {
                hit->u.tabbar.selected_index = idx;
                if (hit->u.tabbar.on_change) {
                    hit->u.tabbar.on_change(hit, (float)idx);
                }
            }
            break;
        }

        case CUIF_WIDGET_BEZIER_EDITOR: {
            int count = hit->u.bezier_editor.node_count;
            float* xs = hit->u.bezier_editor.node_x;
            float* ys = hit->u.bezier_editor.node_y;
            
            int closest = -1;
            float min_dist = 15.0f; /* 15 pixel radius threshold */
            for (int i = 0; i < count; ++i) {
                float nx = hit->x + xs[i] * hit->w;
                float ny = hit->y + ys[i] * hit->h;
                float dx = mx - nx;
                float dy = my - ny;
                float d = sqrtf(dx * dx + dy * dy);
                if (d < min_dist) {
                    min_dist = d;
                    closest = i;
                }
            }
            hit->u.bezier_editor.active_node = closest;
            break;
        }
        
        case CUIF_WIDGET_EQ_CURVE:
            hit->u.eq_curve.active_band = eq_band_at(hit, mx, my);
            if (hit->u.eq_curve.active_band < 0) cuif_window_set_active_widget(cuif_current_window, NULL);
            break;

        case CUIF_WIDGET_READOUT:
            if (!hit->u.readout.editing) readout_begin_edit(hit);
            cuif_window_set_active_widget(cuif_current_window, NULL);
            break;

        default:
            break;
    }
}

void cuif_widget_dispatch_mouse_up(cuif_widget* root, float mx, float my, int button) {
    (void)root;
    (void)mx;
    (void)my;
    (void)button;

    if (!cuif_current_window) return;
    cuif_widget* w = cuif_window_get_active_widget(cuif_current_window);
    if (!w) return;

    cuif_window_set_active_widget(cuif_current_window, NULL);

    switch (w->type) {
        case CUIF_WIDGET_KNOB:
            w->u.knob.dragging = false;
            break;
        case CUIF_WIDGET_SLIDER:
            w->u.slider.dragging = false;
            break;
        case CUIF_WIDGET_BUTTON:
            if (!w->u.button.is_toggle) {
                w->u.button.state = false;
            }
            break;
        case CUIF_WIDGET_BEZIER_EDITOR:
            w->u.bezier_editor.active_node = -1;
            break;
        case CUIF_WIDGET_EQ_CURVE:
            w->u.eq_curve.active_band = -1;
            break;
        default:
            break;
    }
}

static void sort_bezier_nodes(float* xs, float* ys, int count) {
    for (int i = 0; i < count - 1; ++i) {
        for (int j = 0; j < count - i - 1; ++j) {
            if (xs[j] > xs[j+1]) {
                float tx = xs[j]; xs[j] = xs[j+1]; xs[j+1] = tx;
                float ty = ys[j]; ys[j] = ys[j+1]; ys[j+1] = ty;
            }
        }
    }
}

void cuif_widget_dispatch_mouse_move(cuif_widget* root, float mx, float my, float dx, float dy, int button) {
    (void)root;
    (void)dx;
    (void)dy;
    (void)button;

    if (!cuif_current_window) return;
    cuif_widget* w = cuif_window_get_active_widget(cuif_current_window);
    if (!w) return;

    switch (w->type) {
        case CUIF_WIDGET_KNOB: {
            if (!w->u.knob.dragging) break; /* e.g. the moves after a double-click reset */
            float delta_y = my - w->u.knob.drag_start_y;
            float sens = 0.005f;
            float val = w->u.knob.drag_start_val - delta_y * sens;
            if (val < 0.0f) val = 0.0f;
            if (val > 1.0f) val = 1.0f;
            w->u.knob.value = val;
            if (w->u.knob.on_change) w->u.knob.on_change(w, val);
            break;
        }

        case CUIF_WIDGET_SLIDER: {
            if (w->u.slider.is_vertical) {
                float val = 1.0f - (my - w->y - 6.0f) / (w->h - 12.0f);
                if (val < 0.0f) val = 0.0f;
                if (val > 1.0f) val = 1.0f;
                w->u.slider.value = val;
                if (w->u.slider.on_change) w->u.slider.on_change(w, val);
            } else {
                float val = (mx - w->x - 6.0f) / (w->w - 12.0f);
                if (val < 0.0f) val = 0.0f;
                if (val > 1.0f) val = 1.0f;
                w->u.slider.value = val;
                if (w->u.slider.on_change) w->u.slider.on_change(w, val);
            }
            break;
        }

        case CUIF_WIDGET_BEZIER_EDITOR: {
            int active = w->u.bezier_editor.active_node;
            if (active >= 0 && active < w->u.bezier_editor.node_count) {
                float nx = (mx - w->x) / w->w;
                float ny = (my - w->y) / w->h;
                if (nx < 0.0f) nx = 0.0f;
                if (nx > 1.0f) nx = 1.0f;
                if (ny < 0.0f) ny = 0.0f;
                if (ny > 1.0f) ny = 1.0f;
                
                /* Maintain start/end nodes anchored horizontally at 0 and 1 */
                if (active == 0) nx = 0.0f;
                if (active == w->u.bezier_editor.node_count - 1) nx = 1.0f;
                
                w->u.bezier_editor.node_x[active] = nx;
                w->u.bezier_editor.node_y[active] = ny;
                
                /* Keep sorted horizontally */
                sort_bezier_nodes(w->u.bezier_editor.node_x, w->u.bezier_editor.node_y, w->u.bezier_editor.node_count);
                
                /* Update the active node index after sort if order swapped */
                for (int i = 0; i < w->u.bezier_editor.node_count; ++i) {
                    if (w->u.bezier_editor.node_x[i] == nx && w->u.bezier_editor.node_y[i] == ny) {
                        w->u.bezier_editor.active_node = i;
                        break;
                    }
                }
                
                if (w->u.bezier_editor.on_curve_change) {
                    w->u.bezier_editor.on_curve_change(w);
                }
            }
            break;
        }

        case CUIF_WIDGET_EQ_CURVE: {
            int band = w->u.eq_curve.active_band;
            if (band < 0 || band >= w->u.eq_curve.band_count) break;
            w->u.eq_curve.band_hz[band] = eq_x_to_hz(w, mx);
            w->u.eq_curve.band_gain_db[band] = eq_y_to_db(w, my);
            if (w->u.eq_curve.on_band_change) w->u.eq_curve.on_band_change(w, (float)band);
            break;
        }
        
        default:
            break;
    }
}

void cuif_widget_dispatch_mouse_wheel(cuif_widget* root, float mx, float my, float delta_notches) {
    if (!root || delta_notches == 0.0f) return;
    cuif_widget* hit = hit_test(root, mx, my);
    if (!hit) return;

    if (hit->type == CUIF_WIDGET_EQ_CURVE) {
        int band = eq_band_at(hit, mx, my);
        if (band < 0) return;
        float q = hit->u.eq_curve.band_q[band] * powf(CUIF_EQ_WHEEL_Q_FACTOR, delta_notches);
        q = clampf(q, hit->u.eq_curve.min_q, hit->u.eq_curve.max_q);
        if (q == hit->u.eq_curve.band_q[band]) return;
        hit->u.eq_curve.band_q[band] = q;
        if (hit->u.eq_curve.on_band_change) hit->u.eq_curve.on_band_change(hit, (float)band);
        return;
    }

    float* value = NULL;
    cuif_widget_value_changed_fn on_change = NULL;
    switch (hit->type) {
        case CUIF_WIDGET_KNOB:   value = &hit->u.knob.value;   on_change = hit->u.knob.on_change;   break;
        case CUIF_WIDGET_SLIDER: value = &hit->u.slider.value; on_change = hit->u.slider.on_change; break;
        default: return;
    }
    float v = *value + delta_notches * CUIF_WHEEL_STEP;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    if (v == *value) return;
    *value = v;
    if (on_change) on_change(hit, v);
}
