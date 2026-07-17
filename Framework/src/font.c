#define _CRT_SECURE_NO_WARNINGS
#include "cuif/font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <gl/gl.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define CUIF_FONT_FIRST_CHAR 32
#define CUIF_FONT_GLYPH_COUNT 96 /* ASCII 32..127 */
#define CUIF_FONT_ATLAS_SIZE 1024 /* up from the old 512 -- fits a 2x2-oversampled bake comfortably at realistic DPI scales */
#define CUIF_FONT_OVERSAMPLE 2

struct cuif_font {
    GLuint texture_id;
    int bitmap_w;
    int bitmap_h;
    unsigned char* font_file_data; /* kept for cuif_font_rebake() -- stbtt_PackFontRange needs the raw TTF bytes every bake */
    long font_file_size;
    stbtt_packedchar chardata[CUIF_FONT_GLYPH_COUNT];
};

/*
 * glGenerateMipmap is GL 3.0 core -- not in the GL 1.1-era gl/gl.h Windows
 * ships, and not statically linkable via opengl32.lib. Resolved once per
 * process via wglGetProcAddress, same pattern as #80's MSAA bootstrap.
 * Requires a context to be current to resolve correctly, which holds here
 * since cuif_window_create() now makes its context current immediately.
 */
typedef void(WINAPI* PFNCUIFGLGENERATEMIPMAPPROC)(GLenum target);
static bool cuif_gl_ext_resolved = false;
static PFNCUIFGLGENERATEMIPMAPPROC cuif_glGenerateMipmap = NULL;

static void cuif_resolve_gl_extensions_once(void) {
    if (cuif_gl_ext_resolved) return;
    cuif_gl_ext_resolved = true;
    cuif_glGenerateMipmap = (PFNCUIFGLGENERATEMIPMAPPROC)wglGetProcAddress("glGenerateMipmap");
}

static bool cuif_font_bake_internal(cuif_font* font, float effective_px) {
    cuif_resolve_gl_extensions_once();

    unsigned char* bitmap = (unsigned char*)calloc((size_t)CUIF_FONT_ATLAS_SIZE * (size_t)CUIF_FONT_ATLAS_SIZE, 1);
    if (!bitmap) return false;

    stbtt_pack_context pack_ctx;
    if (!stbtt_PackBegin(&pack_ctx, bitmap, CUIF_FONT_ATLAS_SIZE, CUIF_FONT_ATLAS_SIZE, 0, 1, NULL)) {
        free(bitmap);
        return false;
    }

    /* Oversampling improves subpixel positioning quality at a given bake size -- it does not change effective_px itself. */
    stbtt_PackSetOversampling(&pack_ctx, CUIF_FONT_OVERSAMPLE, CUIF_FONT_OVERSAMPLE);

    int result = stbtt_PackFontRange(&pack_ctx, font->font_file_data, 0, effective_px,
                                      CUIF_FONT_FIRST_CHAR, CUIF_FONT_GLYPH_COUNT, font->chardata);
    stbtt_PackEnd(&pack_ctx);

    if (result <= 0) {
        /* Packing overflowed the atlas (or another failure) -- leave the font's previous bake/texture untouched. */
        free(bitmap);
        return false;
    }

    if (font->texture_id == 0) {
        glGenTextures(1, &font->texture_id);
    }
    glBindTexture(GL_TEXTURE_2D, font->texture_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, CUIF_FONT_ATLAS_SIZE, CUIF_FONT_ATLAS_SIZE, 0, GL_ALPHA, GL_UNSIGNED_BYTE, bitmap);

    if (cuif_glGenerateMipmap) {
        cuif_glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    } else {
        /* No mipmap support resolved -- fall back to plain bilinear rather than an unfiltered/broken minification mode. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    free(bitmap);

    font->bitmap_w = CUIF_FONT_ATLAS_SIZE;
    font->bitmap_h = CUIF_FONT_ATLAS_SIZE;
    return true;
}

cuif_font* cuif_font_load(const char* filepath, float font_size) {
    FILE* f = fopen(filepath, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    unsigned char* font_data = (unsigned char*)malloc(size);
    if (!font_data) {
        fclose(f);
        return NULL;
    }

    size_t read_bytes = fread(font_data, 1, size, f);
    fclose(f);
    if (read_bytes < (size_t)size) {
        free(font_data);
        return NULL;
    }

    cuif_font* font = (cuif_font*)calloc(1, sizeof(cuif_font));
    if (!font) {
        free(font_data);
        return NULL;
    }

    font->font_file_data = font_data;
    font->font_file_size = size;

    float effective_px = cuif_font_effective_bake_px(font_size, 1.0f);
    if (!cuif_font_bake_internal(font, effective_px)) {
        free(font->font_file_data);
        free(font);
        return NULL;
    }

    return font;
}

bool cuif_font_rebake(cuif_font* font, float effective_px) {
    if (!font || !font->font_file_data) return false;
    return cuif_font_bake_internal(font, effective_px);
}

void cuif_font_free(cuif_font* font) {
    if (!font) return;
    if (font->texture_id) {
        glDeleteTextures(1, &font->texture_id);
    }
    free(font->font_file_data);
    free(font);
}

void cuif_draw_text(cuif_font* font, const char* text, float x, float y, cuif_color color) {
    if (!font || !text) return;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, font->texture_id);
    glColor4f(color.r, color.g, color.b, color.a);

    glBegin(GL_QUADS);
    while (*text) {
        char c = *text;
        if (c >= CUIF_FONT_FIRST_CHAR && c < CUIF_FONT_FIRST_CHAR + CUIF_FONT_GLYPH_COUNT) {
            stbtt_aligned_quad q;
            /* y points down in our 2D coordinate system. align_to_integer=0: oversampling's quality benefit is
               specifically subpixel positioning, which integer-snapping would throw away. */
            stbtt_GetPackedQuad(font->chardata, font->bitmap_w, font->bitmap_h, c - CUIF_FONT_FIRST_CHAR, &x, &y, &q, 0);

            glTexCoord2f(q.s0, q.t0); glVertex2f(q.x0, q.y0);
            glTexCoord2f(q.s1, q.t0); glVertex2f(q.x1, q.y0);
            glTexCoord2f(q.s1, q.t1); glVertex2f(q.x1, q.y1);
            glTexCoord2f(q.s0, q.t1); glVertex2f(q.x0, q.y1);
        }
        text++;
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

float cuif_font_measure_text(cuif_font* font, const char* text) {
    if (!font || !text) return 0.0f;

    float x = 0.0f, y = 0.0f;
    while (*text) {
        char c = *text;
        if (c >= CUIF_FONT_FIRST_CHAR && c < CUIF_FONT_FIRST_CHAR + CUIF_FONT_GLYPH_COUNT) {
            stbtt_aligned_quad q;
            stbtt_GetPackedQuad(font->chardata, font->bitmap_w, font->bitmap_h, c - CUIF_FONT_FIRST_CHAR, &x, &y, &q, 0);
        }
        text++;
    }
    return x; /* x has accumulated every glyph's advance width */
}
