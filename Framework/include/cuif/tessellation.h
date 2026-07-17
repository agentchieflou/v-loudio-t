#ifndef CUIF_TESSELLATION_H
#define CUIF_TESSELLATION_H

#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Chord-error-bounded segment count for approximating a circular arc as a
 * polyline -- the standard approach used in vector renderers (Skia,
 * NanoVG), replacing the fixed 8/32-segment counts this framework used to
 * hardcode regardless of actual on-screen size.
 *
 * `radius_px` must already be in DEVICE pixels (logical_radius *
 * dpi_scale, see cuif_window_get_dpi_scale()), not logical pixels -- this
 * is what makes tessellation scale up correctly on high-DPI displays
 * instead of getting proportionally *more* faceted as physical pixel
 * density increases.
 */
static inline int cuif_arc_segment_count(float radius_px, float sweep_radians) {
    const float max_chord_error_px = 0.05f; /* sub-pixel deviation from the true circle */
    const int min_segments = 8;             /* floor: matches the framework's old fixed corner-arc count */
    const int max_segments = 128;           /* ceiling: bounds vertex count for very large radii */

    if (radius_px <= 0.0f || sweep_radians <= 0.0f) return min_segments;

    float ratio = 1.0f - (max_chord_error_px / radius_px);
    if (ratio < -1.0f) ratio = -1.0f; /* keep acosf() in its valid domain for small radii */
    if (ratio > 1.0f) ratio = 1.0f;

    float half_theta = acosf(ratio);
    if (half_theta < 1e-6f) return max_segments; /* ratio ~= 1 (huge radius) -- avoid dividing by ~0 below */

    float theta = 2.0f * half_theta;
    int segments = (int)ceilf(sweep_radians / theta);

    if (segments < min_segments) segments = min_segments;
    if (segments > max_segments) segments = max_segments;
    return segments;
}

#ifdef __cplusplus
}
#endif

#endif /* CUIF_TESSELLATION_H */
