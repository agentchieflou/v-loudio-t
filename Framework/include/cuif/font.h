#ifndef CUIF_FONT_H
#define CUIF_FONT_H

#include "cuif/graphics.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cuif_font cuif_font;

/* Loads a TTF file and bakes a glyph atlas. font_size is the height in pixels. */
cuif_font* cuif_font_load(const char* filepath, float font_size);
void cuif_font_free(cuif_font* font);

/* Draws UTF-8 encoded text at the given coordinates. */
void cuif_draw_text(cuif_font* font, const char* text, float x, float y, cuif_color color);

#ifdef __cplusplus
}
#endif

#endif /* CUIF_FONT_H */
