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

void cuif_widget_destroy(cuif_widget* w) {
    if (!w) return;
    for (int i = 0; i < w->child_count; ++i) {
        cuif_widget_destroy(w->children[i]);
    }
    free(w->children);
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
                float label_len = strlen(w->u.knob.label);
                float tx = cx - label_len * 3.5f;
                cuif_draw_text(cuif_global_font, w->u.knob.label, tx, w->y + w->h - 18.0f, text_gray);
                
                char val_str[16];
                sprintf_s(val_str, sizeof(val_str), "%.2f", w->u.knob.value);
                float vx = cx - strlen(val_str) * 3.5f;
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
                float label_len = strlen(w->u.button.label);
                float tx = w->x + (w->w - label_len * 7.0f) * 0.5f;
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
                    float label_len = (float)strlen(w->u.tabbar.tab_labels[i]);
                    float lx = tx + (tab_w - label_len * 7.0f) * 0.5f;
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
    if (!hit) return;

    cuif_window_set_active_widget(cuif_current_window, hit);

    switch (hit->type) {
        case CUIF_WIDGET_KNOB:
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
        
        default:
            break;
    }
}
