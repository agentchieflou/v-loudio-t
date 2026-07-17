#ifndef CUIF_DPI_UTILS_H
#define CUIF_DPI_UTILS_H

#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pure logical<->physical pixel conversion, kept separate from
 * window_win32.c so it's testable without a live window/GL context.
 * `scale` <= 0 is treated as 1.0 (no scaling) by callers -- these
 * functions themselves assume a valid, already-sanitized scale.
 */

static inline int cuif_logical_to_physical_px(int logical, float scale) {
    return (int)lroundf((float)logical * scale);
}

static inline float cuif_physical_to_logical_px(float physical, float scale) {
    return physical / scale;
}

#ifdef __cplusplus
}
#endif

#endif /* CUIF_DPI_UTILS_H */
