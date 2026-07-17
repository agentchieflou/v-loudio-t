#ifndef CUIF_GRAPHICS_H
#define CUIF_GRAPHICS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Matches tessellation.h's max_segments ceiling (+1 for the closing point). Sizes stack buffers for cuif_generate_arc_points(). */
#define CUIF_MAX_ARC_POINTS 129

typedef struct {
    float r, g, b, a;
} cuif_color;

static inline cuif_color cuif_rgba(float r, float g, float b, float a) {
    cuif_color c = {r, g, b, a};
    return c;
}

static inline cuif_color cuif_rgb(float r, float g, float b) {
    return cuif_rgba(r, g, b, 1.0f);
}

void cuif_draw_line(float x1, float y1, float x2, float y2, float thickness, cuif_color color);
void cuif_draw_rect(float x, float y, float w, float h, cuif_color color, bool fill);
void cuif_draw_rounded_rect(float x, float y, float w, float h, float radius, cuif_color color, bool fill);
void cuif_draw_gradient_rect(float x, float y, float w, float h, cuif_color c1, cuif_color c2, bool vertical);
void cuif_draw_bezier(float x1, float y1, float cp1x, float cp1y, float cp2x, float cp2y, float x2, float y2, float thickness, cuif_color color, int segments);

/*
 * Strokes a polyline as per-segment quads with bevel joins, instead of
 * GL_LINES/GL_LINE_STRIP + glLineWidth(). glLineWidth() for values above
 * 1.0 is driver-dependent and commonly clamped in a legacy compatibility-
 * profile GL context (the kind this framework creates) -- this makes
 * requested stroke thickness reliable everywhere. `points_xy` is a flat
 * [x0,y0,x1,y1,...] array of `point_count` points; `closed` connects the
 * last point back to the first.
 */
void cuif_draw_polyline(const float* points_xy, int point_count, float thickness, cuif_color color, bool closed);

/*
 * Writes up to `max_points` points along a circular arc (adaptively
 * tessellated via cuif_arc_segment_count(), see cuif/tessellation.h) into
 * `out_points_xy` as a flat [x0,y0,x1,y1,...] array, and returns the
 * number of points written. Shared by cuif_draw_rounded_rect's corners and
 * widget.c's knob/arc strokes so both stay in sync on tessellation quality.
 */
int cuif_generate_arc_points(float cx, float cy, float r, float start_angle, float end_angle, float* out_points_xy, int max_points);

#ifdef __cplusplus
}
#endif

#endif /* CUIF_GRAPHICS_H */
