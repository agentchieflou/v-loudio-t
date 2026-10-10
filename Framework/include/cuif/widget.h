#ifndef CUIF_WIDGET_H
#define CUIF_WIDGET_H

#include "cuif/graphics.h"
#include "cuif/font.h"
#include "cuif/keys.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CUIF_WIDGET_KNOB,
    CUIF_WIDGET_SLIDER,
    CUIF_WIDGET_BUTTON,
    CUIF_WIDGET_DROPDOWN,
    CUIF_WIDGET_BEZIER_EDITOR,
    CUIF_WIDGET_ANALYZER,
    CUIF_WIDGET_CONTAINER,
    CUIF_WIDGET_TABBAR,
    CUIF_WIDGET_METER,          /* level / gain-reduction bar with ballistics (#131) */
    CUIF_WIDGET_TRANSFER_CURVE, /* in->out dB curve of a dynamics processor (#131) */
    CUIF_WIDGET_EQ_CURVE,       /* frequency response with draggable band handles (#131) */
    CUIF_WIDGET_READOUT         /* numeric value box with click-to-type entry (#131) */
} cuif_widget_type;

/* Longest text a readout accepts while editing, NUL included. */
#define CUIF_READOUT_TEXT_MAX 24

/* Maximum bands an EQ curve widget tracks. */
#define CUIF_EQ_CURVE_MAX_BANDS 8

typedef struct cuif_widget cuif_widget;

typedef void (*cuif_widget_value_changed_fn)(cuif_widget* w, float value);

struct cuif_widget {
    cuif_widget_type type;
    float x, y, w, h;
    bool visible;
    bool enabled;
    
    cuif_widget* parent;
    cuif_widget** children;
    int child_count;
    int child_capacity;

    void* user_data;

    union {
        struct {
            float value;         /* 0.0 to 1.0 */
            float default_value; /* 0.0 to 1.0 */
            char label[32];
            cuif_widget_value_changed_fn on_change;
            bool dragging;
            float drag_start_val;
            float drag_start_y;
        } knob;

        struct {
            float value;         /* 0.0 to 1.0 */
            bool is_vertical;
            cuif_widget_value_changed_fn on_change;
            bool dragging;
            float drag_start_val;
            float drag_start_pos;
        } slider;

        struct {
            char label[64];
            bool is_toggle;
            bool state;
            void (*on_click)(cuif_widget* w);
        } button;

        struct {
            const char** items;
            int item_count;
            int selected_index;
            bool is_open;
            cuif_widget_value_changed_fn on_change;
        } dropdown;

        struct {
            float node_x[8]; /* 0.0 to 1.0 */
            float node_y[8]; /* 0.0 to 1.0 */
            int node_count;
            int active_node;
            void (*on_curve_change)(cuif_widget* w);
        } bezier_editor;

        struct {
            float* buffer;
            int buffer_size;
            cuif_color line_color;
        } analyzer;

        struct {
            const char** tab_labels;
            int tab_count;
            int selected_index;
            cuif_widget_value_changed_fn on_change;
        } tabbar;

        /*
         * Level meter. The host feeds an instantaneous level in dB from
         * the UI thread (cuif_widget_meter_feed); the widget owns the
         * ballistics: the bar rises instantly, falls at fall_db_per_second,
         * and a peak marker holds for peak_hold_ms before falling with it.
         * Time advances in cuif_widget_meter_step(), which render calls
         * from the wall clock and tests call directly.
         */
        struct {
            float min_db, max_db;
            bool is_vertical;
            bool fill_from_top;        /* gain-reduction style: bar grows from the max end */
            float fed_db;              /* last value the host fed */
            float value_db;            /* displayed bar level */
            float peak_db;             /* displayed peak marker */
            float fall_db_per_second;  /* default 24 */
            float peak_hold_ms;        /* default 1500 */
            float peak_age_ms;
            unsigned long long last_step_ms; /* 0 = not yet stepped */
        } meter;

        /*
         * Static transfer curve of a dynamics processor, in dB in -> dB
         * out, with the live operating point. The curve is the same
         * quadratic-knee formula as loudio::GainComputer
         * (DSP/include/loudio/dsp/GainComputer.h), evaluated here so the
         * widget needs no DSP code; cuif_widget_transfer_curve_output_db()
         * exposes it for tests.
         */
        struct {
            float min_db, max_db;      /* both axes */
            float threshold_db;
            float ratio;               /* >= 1; very large = limiter */
            float knee_db;
            float range_db;            /* expander floor; 0 = none */
            bool expander;             /* false = compressor */
            float point_in_db;         /* operating point (fed by the host) */
            float point_out_db;
            bool has_point;
        } transfer_curve;

        /*
         * Frequency-response display with one draggable handle per band:
         * x = log frequency, y = gain dB; dragging moves a band's
         * frequency and gain, the wheel over a handle scales its Q.
         * The response curve itself is host-supplied (response_db, one dB
         * value per point, log-spaced from min_hz to max_hz) so the exact
         * filter math stays in the plugin; optional, NULL draws no curve.
         * on_band_change(w, band) fires after every edit.
         */
        struct {
            int band_count;
            float band_hz[CUIF_EQ_CURVE_MAX_BANDS];
            float band_gain_db[CUIF_EQ_CURVE_MAX_BANDS];
            float band_q[CUIF_EQ_CURVE_MAX_BANDS];
            bool band_enabled[CUIF_EQ_CURVE_MAX_BANDS];
            float min_hz, max_hz;
            float min_db, max_db;
            float min_q, max_q;
            const float* response_db;
            int response_count;
            int active_band;           /* being dragged, or -1 */
            bool draw_background;      /* false: transparent, to layer over an analyzer */
            cuif_widget_value_changed_fn on_band_change; /* value = band index */
        } eq_curve;

        /*
         * Numeric readout: shows `value` as "%.<decimals>f<unit>". A click
         * starts text entry (the widget takes keyboard focus): digits, '-',
         * '.', ',' (read as '.'), Backspace edit the text; Enter parses it,
         * clamps to [min, max] and fires on_commit(w, value); Escape, a
         * click elsewhere or loss of keyboard focus cancels. Hosts push the
         * displayed value with cuif_widget_readout_set_value (never
         * on_commit). The first keyboard-driven widget in cuif.
         */
        struct {
            float value;
            float min_value, max_value;
            int decimals;
            char unit[8];
            bool editing;
            char text[CUIF_READOUT_TEXT_MAX];
            int text_len;
            cuif_widget_value_changed_fn on_commit;
        } readout;
    } u;
};

/* Global font used by widgets for text rendering */
extern cuif_font* cuif_global_font;

/* Widget Creation Functions */
cuif_widget* cuif_widget_create_container(float x, float y, float w, float h);
cuif_widget* cuif_widget_create_knob(float x, float y, float w, float h, const char* label, float default_val, cuif_widget_value_changed_fn on_change);
cuif_widget* cuif_widget_create_slider(float x, float y, float w, float h, bool is_vertical, cuif_widget_value_changed_fn on_change);
cuif_widget* cuif_widget_create_button(float x, float y, float w, float h, const char* label, bool is_toggle, void (*on_click)(cuif_widget* w));
cuif_widget* cuif_widget_create_dropdown(float x, float y, float w, float h, const char** items, int item_count, cuif_widget_value_changed_fn on_change);
cuif_widget* cuif_widget_create_bezier_editor(float x, float y, float w, float h, int num_nodes, void (*on_curve_change)(cuif_widget* w));
cuif_widget* cuif_widget_create_analyzer(float x, float y, float w, float h, float* buffer, int buffer_size, cuif_color color);
/*
 * A row of equal-width labeled tabs. Tracks a selected index and fires
 * on_change with the newly selected index when clicked -- it does not
 * manage page/content switching itself. The conventional pattern is to
 * give each "page" its own CUIF_WIDGET_CONTAINER child and toggle that
 * container's `visible` flag from the on_change callback.
 */
cuif_widget* cuif_widget_create_tabbar(float x, float y, float w, float h, const char** labels, int tab_count, cuif_widget_value_changed_fn on_change);

/* Meter (#131). min_db/max_db set the scale (e.g. -60..0 for level, 0..-30
 * top-down for gain reduction with fill_from_top). */
cuif_widget* cuif_widget_create_meter(float x, float y, float w, float h, float min_db, float max_db, bool is_vertical);
/* UI thread, any rate. Values outside [min_db, max_db] are clamped. */
void cuif_widget_meter_feed(cuif_widget* w, float level_db);
/* Advances the ballistics by dt_seconds. render() calls this from the wall clock. */
void cuif_widget_meter_step(cuif_widget* w, float dt_seconds);

/* Transfer curve (#131). Axes span min_db..max_db; starts as a 1:1 line. */
cuif_widget* cuif_widget_create_transfer_curve(float x, float y, float w, float h, float min_db, float max_db);
void cuif_widget_transfer_curve_set(cuif_widget* w, float threshold_db, float ratio, float knee_db, float range_db, bool expander);
void cuif_widget_transfer_curve_feed(cuif_widget* w, float in_db, float out_db);
/* The curve the widget draws, as a function: same formula as loudio::GainComputer. */
float cuif_widget_transfer_curve_output_db(const cuif_widget* w, float in_db);

/* EQ curve (#131). Bands start disabled at 1 kHz, 0 dB, Q 0.707; set them
 * with cuif_widget_eq_curve_set_band. */
cuif_widget* cuif_widget_create_eq_curve(float x, float y, float w, float h, int band_count,
                                         float min_hz, float max_hz, float min_db, float max_db,
                                         cuif_widget_value_changed_fn on_band_change);
void cuif_widget_eq_curve_set_band(cuif_widget* w, int band, float hz, float gain_db, float q, bool enabled);
void cuif_widget_eq_curve_set_response(cuif_widget* w, const float* response_db, int count);
/* Handle position for a band, in the widget's logical coordinates (tests and layout). */
void cuif_widget_eq_curve_handle_position(const cuif_widget* w, int band, float* out_x, float* out_y);

/* Readout (#131). unit may be NULL or up to 7 characters (e.g. " dB"). */
cuif_widget* cuif_widget_create_readout(float x, float y, float w, float h, float min_value, float max_value,
                                        int decimals, const char* unit, cuif_widget_value_changed_fn on_commit);
/* Sets the displayed value (clamped). Ignored while the user is typing. */
void cuif_widget_readout_set_value(cuif_widget* w, float value);
/* The text the widget shows right now (display or edit buffer), for tests. */
void cuif_widget_readout_text(const cuif_widget* w, char* out, size_t out_size);

void cuif_widget_destroy(cuif_widget* w);
void cuif_widget_add_child(cuif_widget* parent, cuif_widget* child);

/* Core render loop for widget tree */
void cuif_widget_render(cuif_widget* w);

/* Event routing functions */
void cuif_widget_dispatch_mouse_down(cuif_widget* root, float mx, float my, int button);
void cuif_widget_dispatch_mouse_up(cuif_widget* root, float mx, float my, int button);
void cuif_widget_dispatch_mouse_move(cuif_widget* root, float mx, float my, float dx, float dy, int button);

/*
 * Mouse wheel over whatever widget is under (mx, my). delta_notches is in
 * wheel notches: +1 is one notch away from the user ("up"). Knobs and
 * sliders step their value by CUIF_WHEEL_STEP per notch (clamped to 0..1)
 * and fire on_change; an EQ curve scales the Q of the band handle under
 * the cursor by CUIF_EQ_WHEEL_Q_FACTOR per notch; other widgets ignore it.
 */
#define CUIF_WHEEL_STEP 0.02f
#define CUIF_EQ_WHEEL_Q_FACTOR 1.1f
/* Pixel radius within which a click grabs an EQ band handle. */
#define CUIF_EQ_HANDLE_RADIUS 10.0f
void cuif_widget_dispatch_mouse_wheel(cuif_widget* root, float mx, float my, float delta_notches);

/*
 * Keyboard input (#131; codes in cuif/keys.h). Goes to the window's focused
 * widget (set by a click on a readout, see cuif_window_set_focused_widget),
 * never by position; returns true if that widget consumed it. CHAR events
 * carry a UTF-32 code point in `key`; DOWN/UP carry a CUIF_VK_* code.
 */
bool cuif_widget_dispatch_key(cuif_widget* root, int type, uint32_t key, uint32_t modifiers);

/* Ends any text entry in progress without committing (keyboard focus lost,
 * editor closed, click elsewhere). Safe with no focused widget. */
void cuif_widget_cancel_text_entry(void);

/*
 * Double-clicking a knob resets it to its default_value (and fires
 * on_change) instead of starting a drag. Detection is done inside
 * cuif_widget_dispatch_mouse_down from two downs on the same widget within
 * the double-click time and within 4 logical px of each other, so it works
 * for real WM_LBUTTONDOWN pairs and injected ones alike. 0 (the default)
 * uses the system double-click time; tests pin a value.
 */
void cuif_widget_set_double_click_time_ms(unsigned ms);

#ifdef __cplusplus
}
#endif

#endif /* CUIF_WIDGET_H */
