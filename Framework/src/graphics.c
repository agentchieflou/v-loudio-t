#include "cuif/graphics.h"
#include "cuif/tessellation.h"
#include "cuif/window.h"

#include <windows.h>
#include <gl/gl.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int cuif_generate_arc_points(float cx, float cy, float r, float start_angle, float end_angle, float* out_points_xy, int max_points) {
    if (!out_points_xy || max_points <= 0) return 0;

    float dpi_scale = cuif_current_window ? cuif_window_get_dpi_scale(cuif_current_window) : 1.0f;
    int segments = cuif_arc_segment_count(r * dpi_scale, fabsf(end_angle - start_angle));

    int point_count = segments + 1;
    if (point_count > max_points) point_count = max_points;

    for (int i = 0; i < point_count; ++i) {
        float t = (point_count > 1) ? (float)i / (float)(point_count - 1) : 0.0f;
        float theta = start_angle + (end_angle - start_angle) * t;
        out_points_xy[i * 2] = cx + r * cosf(theta);
        out_points_xy[i * 2 + 1] = cy + r * sinf(theta);
    }

    return point_count;
}

void cuif_draw_polyline(const float* points_xy, int point_count, float thickness, cuif_color color, bool closed) {
    if (!points_xy || point_count < 2 || thickness <= 0.0f) return;

    float half_thickness = thickness * 0.5f;

    glColor4f(color.r, color.g, color.b, color.a);

    /* One quad per segment, offset by the segment's perpendicular normal. */
    glBegin(GL_QUADS);
    int segment_count = closed ? point_count : point_count - 1;
    for (int i = 0; i < segment_count; ++i) {
        int i0 = i;
        int i1 = (i + 1) % point_count;

        float x0 = points_xy[i0 * 2], y0 = points_xy[i0 * 2 + 1];
        float x1 = points_xy[i1 * 2], y1 = points_xy[i1 * 2 + 1];

        float dx = x1 - x0, dy = y1 - y0;
        float len = sqrtf(dx * dx + dy * dy);
        if (len < 1e-6f) continue; /* degenerate/zero-length segment */

        float nx = -dy / len * half_thickness;
        float ny = dx / len * half_thickness;

        glVertex2f(x0 + nx, y0 + ny);
        glVertex2f(x1 + nx, y1 + ny);
        glVertex2f(x1 - nx, y1 - ny);
        glVertex2f(x0 - nx, y0 - ny);
    }
    glEnd();

    /*
     * Bevel joins: without these, adjacent segment quads leave a small
     * wedge-shaped gap at each interior vertex (more visible the sharper
     * the turn). Filled on both sides rather than picking the correct
     * (convex) side via a turn-direction test -- simpler, and at these
     * stroke thicknesses (1-4.5px) any resulting sub-pixel double-blend
     * on the concave side of a semi-transparent stroke is not visible in
     * practice, especially with MSAA already smoothing the edges.
     */
    int joint_start = closed ? 0 : 1;
    int joint_end = closed ? point_count : point_count - 1;
    if (joint_end > joint_start) {
        glBegin(GL_TRIANGLES);
        for (int i = joint_start; i < joint_end; ++i) {
            int iprev = (i - 1 + point_count) % point_count;
            int inext = (i + 1) % point_count;

            float xp = points_xy[iprev * 2], yp = points_xy[iprev * 2 + 1];
            float xc = points_xy[i * 2], yc = points_xy[i * 2 + 1];
            float xn = points_xy[inext * 2], yn = points_xy[inext * 2 + 1];

            float dx1 = xc - xp, dy1 = yc - yp;
            float len1 = sqrtf(dx1 * dx1 + dy1 * dy1);
            float dx2 = xn - xc, dy2 = yn - yc;
            float len2 = sqrtf(dx2 * dx2 + dy2 * dy2);
            if (len1 < 1e-6f || len2 < 1e-6f) continue;

            float n1x = -dy1 / len1 * half_thickness, n1y = dx1 / len1 * half_thickness;
            float n2x = -dy2 / len2 * half_thickness, n2y = dx2 / len2 * half_thickness;

            glVertex2f(xc, yc);
            glVertex2f(xc + n1x, yc + n1y);
            glVertex2f(xc + n2x, yc + n2y);

            glVertex2f(xc, yc);
            glVertex2f(xc - n1x, yc - n1y);
            glVertex2f(xc - n2x, yc - n2y);
        }
        glEnd();
    }
}

void cuif_draw_line(float x1, float y1, float x2, float y2, float thickness, cuif_color color) {
    float points[4] = { x1, y1, x2, y2 };
    cuif_draw_polyline(points, 2, thickness, color, false);
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

    float corner_points[CUIF_MAX_ARC_POINTS * 2];
    int n;

    /* Top-Right corner: angles 270 deg to 360 deg */
    n = cuif_generate_arc_points(x + w - radius, y + radius, radius, 1.5f * (float)M_PI, 2.0f * (float)M_PI, corner_points, CUIF_MAX_ARC_POINTS);
    for (int i = 0; i < n; ++i) glVertex2f(corner_points[i * 2], corner_points[i * 2 + 1]);

    /* Bottom-Right corner: angles 0 deg to 90 deg */
    n = cuif_generate_arc_points(x + w - radius, y + h - radius, radius, 0.0f, 0.5f * (float)M_PI, corner_points, CUIF_MAX_ARC_POINTS);
    for (int i = 0; i < n; ++i) glVertex2f(corner_points[i * 2], corner_points[i * 2 + 1]);

    /* Bottom-Left corner: angles 90 deg to 180 deg */
    n = cuif_generate_arc_points(x + radius, y + h - radius, radius, 0.5f * (float)M_PI, 1.0f * (float)M_PI, corner_points, CUIF_MAX_ARC_POINTS);
    for (int i = 0; i < n; ++i) glVertex2f(corner_points[i * 2], corner_points[i * 2 + 1]);

    /* Top-Left corner: angles 180 deg to 270 deg */
    n = cuif_generate_arc_points(x + radius, y + radius, radius, 1.0f * (float)M_PI, 1.5f * (float)M_PI, corner_points, CUIF_MAX_ARC_POINTS);
    for (int i = 0; i < n; ++i) glVertex2f(corner_points[i * 2], corner_points[i * 2 + 1]);

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
    if (segments > CUIF_MAX_ARC_POINTS - 1) segments = CUIF_MAX_ARC_POINTS - 1;

    float points[CUIF_MAX_ARC_POINTS * 2];
    int point_count = segments + 1;

    for (int i = 0; i < point_count; ++i) {
        float t = (float)i / segments;
        float u = 1.0f - t;
        float tt = t * t;
        float uu = u * u;
        float uuu = uu * u;
        float ttt = tt * t;

        points[i * 2] = uuu * x1 + 3.0f * uu * t * cp1x + 3.0f * u * tt * cp2x + ttt * x2;
        points[i * 2 + 1] = uuu * y1 + 3.0f * uu * t * cp1y + 3.0f * u * tt * cp2y + ttt * y2;
    }

    cuif_draw_polyline(points, point_count, thickness, color, false);
}
