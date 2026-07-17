#ifndef CUIF_FONT_H
#define CUIF_FONT_H

#include "cuif/graphics.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cuif_font cuif_font;

/* Loads a TTF file and bakes a glyph atlas. font_size is the height in logical pixels. */
cuif_font* cuif_font_load(const char* filepath, float font_size);
void cuif_font_free(cuif_font* font);

/*
 * Re-bakes an already-loaded font's atlas at a new effective pixel size
 * (see cuif_font_effective_bake_px() below) -- e.g. in response to a
 * display scale change, so glyphs stay crisp at the new device pixel
 * density instead of being GPU-upscaled from the original bake.
 * Returns false (and leaves the font unchanged) if the re-bake fails.
 */
bool cuif_font_rebake(cuif_font* font, float effective_px);

/* Draws UTF-8 encoded text at the given coordinates. */
void cuif_draw_text(cuif_font* font, const char* text, float x, float y, cuif_color color);

/* Width in logical pixels the given text would occupy if drawn with cuif_draw_text -- real glyph advances, not a guess. */
float cuif_font_measure_text(cuif_font* font, const char* text);

/*
 * The actual pixel size to bake/re-bake a font at, given a logical design
 * size (e.g. 13.0f) and the current display scale factor -- design_px *
 * dpi_scale, clamped to a sane ceiling so a pathological scale value can't
 * request an unreasonably large atlas bake. Oversampling (see font.c) is
 * applied separately by the packer and does not further inflate this
 * value -- it improves subpixel quality at a given size, it isn't a size
 * multiplier.
 */
static inline float cuif_font_effective_bake_px(float design_px, float dpi_scale) {
    if (design_px <= 0.0f) design_px = 1.0f;
    if (dpi_scale <= 0.0f) dpi_scale = 1.0f;

    float effective = design_px * dpi_scale;

    const float max_effective_px = 128.0f; /* keeps a 96-glyph, 2x2-oversampled atlas comfortably within a 1024x1024 texture */
    if (effective > max_effective_px) effective = max_effective_px;

    return effective;
}

#ifdef __cplusplus
}
#endif

#endif /* CUIF_FONT_H */
