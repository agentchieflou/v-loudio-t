#ifndef CUIF_THEME_H
#define CUIF_THEME_H

#include "cuif/graphics.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Named color palette consumed by cuif_widget_render() (Framework/src/widget.c)
 * instead of the hardcoded constants it used to define locally. Swapping the
 * active theme via cuif_set_theme() re-colors every widget on the next
 * render -- no per-widget color state to update.
 */
typedef struct {
    cuif_color background;            /* dark background behind bezier/analyzer graphs */
    cuif_color panel_bg;               /* knob cap, inactive button, tab body fills */
    cuif_color border;                  /* borders/outlines, knob track */
    cuif_color primary;                  /* accent: active value arcs, active button, selected tab underline */
    cuif_color text_primary;              /* bright text */
    cuif_color text_secondary;             /* dim/label text */
    cuif_color grid_line;                   /* graph grid lines (bezier editor / analyzer) */
    cuif_color dropdown_overlay_bg;
    cuif_color dropdown_overlay_border;
    cuif_color dropdown_hover_bg;
} cuif_theme;

/* The baseline palette -- matches the values cuif_widget_render used to hardcode, so this is a visual no-op. */
extern const cuif_theme CUIF_THEME_DEFAULT;

void cuif_set_theme(const cuif_theme* theme);
const cuif_theme* cuif_get_theme(void);

#ifdef __cplusplus
}
#endif

#endif /* CUIF_THEME_H */
