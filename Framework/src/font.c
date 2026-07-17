#define _CRT_SECURE_NO_WARNINGS
#include "cuif/font.h"

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <gl/gl.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

struct cuif_font {
    GLuint texture_id;
    int bitmap_w;
    int bitmap_h;
    float font_size;
    stbtt_bakedchar chardata[96]; /* bake ASCII 32..127 */
};

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

    cuif_font* font = (cuif_font*)malloc(sizeof(cuif_font));
    if (!font) {
        free(font_data);
        return NULL;
    }

    font->font_size = font_size;
    font->bitmap_w = 512;
    font->bitmap_h = 512;

    unsigned char* bitmap = (unsigned char*)calloc(font->bitmap_w * font->bitmap_h, 1);
    if (!bitmap) {
        free(font_data);
        free(font);
        return NULL;
    }

    int result = stbtt_BakeFontBitmap(font_data, 0, font_size, bitmap, font->bitmap_w, font->bitmap_h, 32, 96, font->chardata);
    free(font_data);

    if (result <= 0) {
        free(bitmap);
        free(font);
        return NULL;
    }

    /* Upload to OpenGL 1-channel alpha texture */
    glGenTextures(1, &font->texture_id);
    glBindTexture(GL_TEXTURE_2D, font->texture_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, font->bitmap_w, font->bitmap_h, 0, GL_ALPHA, GL_UNSIGNED_BYTE, bitmap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    free(bitmap);
    return font;
}

void cuif_font_free(cuif_font* font) {
    if (!font) return;
    if (font->texture_id) {
        glDeleteTextures(1, &font->texture_id);
    }
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
        if (c >= 32 && c < 128) {
            stbtt_aligned_quad q;
            /* y points down in our 2D coordinate system */
            stbtt_GetBakedQuad(font->chardata, font->bitmap_w, font->bitmap_h, c - 32, &x, &y, &q, 1);
            
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
