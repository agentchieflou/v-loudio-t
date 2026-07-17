#include "cuif/graphics.h"

#include <windows.h>
#include <gl/gl.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void cuif_draw_line(float x1, float y1, float x2, float y2, float thickness, cuif_color color) {
    glColor4f(color.r, color.g, color.b, color.a);
    glLineWidth(thickness);
    glBegin(GL_LINES);
    glVertex2f(x1, y1);
    glVertex2f(x2, y2);
    glEnd();
}

void cuif_draw_rect(float x, float y, float w, float h, cuif_color color, bool fill) {
    glColor4f(color.r, color.g, color.b, color.a);
    if (fill) {
        glBegin(GL_QUADS);
        glVertex2f(x, y);
        glVertex2f(x + w, y);
        glVertex2f(x + w, y + h);
        glVertex2f(x, y + h);
        glEnd();
    } else {
        glBegin(GL_LINE_LOOP);
        glVertex2f(x, y);
        glVertex2f(x + w, y);
        glVertex2f(x + w, y + h);
        glVertex2f(x, y + h);
        glEnd();
    }
}

static void emit_arc(float cx, float cy, float r, float start_angle, float end_angle, int segments) {
    for (int i = 0; i <= segments; ++i) {
        float theta = start_angle + (end_angle - start_angle) * ((float)i / segments);
        glVertex2f(cx + r * cosf(theta), cy + r * sinf(theta));
    }
}

void cuif_draw_rounded_rect(float x, float y, float w, float h, float radius, cuif_color color, bool fill) {
    if (radius <= 0.0f) {
        cuif_draw_rect(x, y, w, h, color, fill);
        return;
    }

    float max_r = (w < h ? w : h) * 0.5f;
    if (radius > max_r) radius = max_r;

    glColor4f(color.r, color.g, color.b, color.a);
    if (fill) {
        glBegin(GL_POLYGON);
    } else {
        glBegin(GL_LINE_LOOP);
    }

    int segs = 8;
    /* Top-Right corner: angles 270 deg to 360 deg */
    emit_arc(x + w - radius, y + radius, radius, 1.5f * (float)M_PI, 2.0f * (float)M_PI, segs);
    /* Bottom-Right corner: angles 0 deg to 90 deg */
    emit_arc(x + w - radius, y + h - radius, radius, 0.0f, 0.5f * (float)M_PI, segs);
    /* Bottom-Left corner: angles 90 deg to 180 deg */
    emit_arc(x + radius, y + h - radius, radius, 0.5f * (float)M_PI, 1.0f * (float)M_PI, segs);
    /* Top-Left corner: angles 180 deg to 270 deg */
    emit_arc(x + radius, y + radius, radius, 1.0f * (float)M_PI, 1.5f * (float)M_PI, segs);

    glEnd();
}

void cuif_draw_gradient_rect(float x, float y, float w, float h, cuif_color c1, cuif_color c2, bool vertical) {
    glBegin(GL_QUADS);
    if (vertical) {
        /* Top-left */
        glColor4f(c1.r, c1.g, c1.b, c1.a);
        glVertex2f(x, y);
        /* Top-right */
        glColor4f(c1.r, c1.g, c1.b, c1.a);
        glVertex2f(x + w, y);
        /* Bottom-right */
        glColor4f(c2.r, c2.g, c2.b, c2.a);
        glVertex2f(x + w, y + h);
        /* Bottom-left */
        glColor4f(c2.r, c2.g, c2.b, c2.a);
        glVertex2f(x, y + h);
    } else {
        /* Top-left */
        glColor4f(c1.r, c1.g, c1.b, c1.a);
        glVertex2f(x, y);
        /* Top-right */
        glColor4f(c2.r, c2.g, c2.b, c2.a);
        glVertex2f(x + w, y);
        /* Bottom-right */
        glColor4f(c2.r, c2.g, c2.b, c2.a);
        glVertex2f(x + w, y + h);
        /* Bottom-left */
        glColor4f(c1.r, c1.g, c1.b, c1.a);
        glVertex2f(x, y + h);
    }
    glEnd();
}

void cuif_draw_bezier(float x1, float y1, float cp1x, float cp1y, float cp2x, float cp2y, float x2, float y2, float thickness, cuif_color color, int segments) {
    if (segments < 2) segments = 2;
    glColor4f(color.r, color.g, color.b, color.a);
    glLineWidth(thickness);
    glBegin(GL_LINE_STRIP);
    for (int i = 0; i <= segments; ++i) {
        float t = (float)i / segments;
        float u = 1.0f - t;
        float tt = t * t;
        float uu = u * u;
        float uuu = uu * u;
        float ttt = tt * t;

        float x = uuu * x1 + 3.0f * uu * t * cp1x + 3.0f * u * tt * cp2x + ttt * x2;
        float y = uuu * y1 + 3.0f * uu * t * cp1y + 3.0f * u * tt * cp2y + ttt * y2;
        glVertex2f(x, y);
    }
    glEnd();
}
