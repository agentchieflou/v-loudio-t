#ifndef CUIF_WIDGET_H
#define CUIF_WIDGET_H

#include "cuif/graphics.h"
#include "cuif/font.h"

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
    CUIF_WIDGET_CONTAINER
} cuif_widget_type;

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

void cuif_widget_destroy(cuif_widget* w);
void cuif_widget_add_child(cuif_widget* parent, cuif_widget* child);

/* Core render loop for widget tree */
void cuif_widget_render(cuif_widget* w);

/* Event routing functions */
void cuif_widget_dispatch_mouse_down(cuif_widget* root, float mx, float my, int button);
void cuif_widget_dispatch_mouse_up(cuif_widget* root, float mx, float my, int button);
void cuif_widget_dispatch_mouse_move(cuif_widget* root, float mx, float my, float dx, float dy, int button);

#ifdef __cplusplus
}
#endif

#endif /* CUIF_WIDGET_H */
