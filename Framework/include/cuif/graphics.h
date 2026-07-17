#ifndef CUIF_GRAPHICS_H
#define CUIF_GRAPHICS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif /* CUIF_GRAPHICS_H */
