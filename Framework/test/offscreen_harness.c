/*
 * Standalone offscreen rendering harness (#99/B2). Unlike test/harness.c
 * (which opens a real, visible top-level window), this exercises
 * cuif_window_create_offscreen() end to end: build a widget tree, render
 * into the FBO, inject synthetic mouse input to actually change a widget's
 * value, render again, and dump both frames to disk as BMP files -- since
 * there's no visible window to screenshot the normal way (the established
 * PrintWindow + PID-verified method this repo otherwise uses), this is the
 * equivalent real-build-and-look-at-the-output verification for a headless
 * render target.
 *
 * Usage: offscreen_harness.exe <output_directory>
 */

#include "cuif/cuif.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void on_knob_change(cuif_widget* w, float val) {
    (void)w;
    printf("  Knob value changed to %.3f via injected drag.\n", val);
}

/*
 * Minimal 24bpp uncompressed BMP writer. glReadPixels() returns rows
 * bottom-up (OpenGL's native row order) and RGBA channel order; a BMP with
 * a positive height is ALSO stored bottom-up, so only the per-pixel
 * RGBA->BGR channel reorder (and dropping alpha, which a 24bpp BMP has no
 * room for) is needed -- no row flip required.
 */
static bool write_bmp_rgba(const char* path, const unsigned char* rgba, int width, int height) {
    int row_stride = (width * 3 + 3) & ~3; /* rows padded to a 4-byte boundary, per the BMP format */
    long pixel_data_size = (long)row_stride * height;
    long file_size = 14 + 40 + pixel_data_size;

    FILE* f = fopen(path, "wb");
    if (!f) return false;

    unsigned char file_header[14] = {0};
    file_header[0] = 'B'; file_header[1] = 'M';
    *(unsigned int*)(file_header + 2) = (unsigned int)file_size;
    *(unsigned int*)(file_header + 10) = 14 + 40; /* offset to pixel data */
    fwrite(file_header, 1, sizeof(file_header), f);

    unsigned char info_header[40] = {0};
    *(unsigned int*)(info_header + 0) = 40; /* header size */
    *(int*)(info_header + 4) = width;
    *(int*)(info_header + 8) = height; /* positive => bottom-up */
    *(unsigned short*)(info_header + 12) = 1;  /* planes */
    *(unsigned short*)(info_header + 14) = 24; /* bitcount */
    fwrite(info_header, 1, sizeof(info_header), f);

    unsigned char* row = (unsigned char*)malloc((size_t)row_stride);
    memset(row, 0, (size_t)row_stride);
    for (int y = 0; y < height; ++y) {
        const unsigned char* src_row = rgba + (size_t)y * width * 4;
        for (int x = 0; x < width; ++x) {
            row[x * 3 + 0] = src_row[x * 4 + 2]; /* B */
            row[x * 3 + 1] = src_row[x * 4 + 1]; /* G */
            row[x * 3 + 2] = src_row[x * 4 + 0]; /* R */
        }
        fwrite(row, 1, (size_t)row_stride, f);
    }
    free(row);

    fclose(f);
    return true;
}

static bool render_and_dump(cuif_window* win, int physical_w, int physical_h, const char* out_path) {
    cuif_window_render_frame(win);

    size_t buf_size = (size_t)physical_w * physical_h * 4;
    unsigned char* pixels = (unsigned char*)malloc(buf_size);
    if (!pixels) return false;

    bool ok = cuif_window_read_pixels(win, pixels, buf_size);
    if (ok) ok = write_bmp_rgba(out_path, pixels, physical_w, physical_h);

    free(pixels);
    return ok;
}

int main(int argc, char** argv) {
    const char* out_dir = argc > 1 ? argv[1] : ".";

    printf("==============================\n");
    printf("cuif Offscreen Rendering Harness\n");
    printf("==============================\n");

    const int logical_w = 300, logical_h = 200;
    const float dpi_scale = 1.5f; /* deliberately non-1.0, to exercise the same DPI path an offscreen window shares with a real one */

    /*
     * Window (and its GL context) must exist and be made current BEFORE
     * loading the font -- cuif_font_load()'s texture upload
     * (glGenTextures/glTexImage2D) needs a current context, and
     * cuif_window_create_offscreen() (like cuif_window_create()) makes its
     * context current as the last step of construction. Loading the font
     * first would run those GL calls with nothing current at all -- the
     * exact class of bug the "make context current immediately" fix
     * earlier in this file exists to prevent for the on-screen path.
     */
    cuif_window* win = cuif_window_create_offscreen(logical_w, logical_h, dpi_scale);
    if (!win) {
        printf("FATAL: cuif_window_create_offscreen failed.\n");
        return 1;
    }
    printf("Created a %dx%d logical (%.1fx DPI) offscreen window.\n", logical_w, logical_h, dpi_scale);

    const float kFontDesignSizePx = 13.0f;
    cuif_global_font = cuif_font_load("C:\\Windows\\Fonts\\arial.ttf", kFontDesignSizePx);
    if (!cuif_global_font) {
        printf("FATAL: could not load Arial.\n");
        return 1;
    }
    /* Same rebake-for-scale step VSTs/Reverb/src/PluginEditor.cpp does on
     * window creation -- the atlas is baked once at 1.0x by cuif_font_load()
     * above; without this, text renders at the wrong effective resolution
     * for a non-1.0x scale (the exact "blurry until rebaked" class of bug
     * Epic 78 fixed for the on-screen path). */
    cuif_font_rebake(cuif_global_font, cuif_font_effective_bake_px(kFontDesignSizePx, dpi_scale));

    cuif_widget* root = cuif_widget_create_container(0, 0, (float)logical_w, (float)logical_h);
    cuif_window_set_root_widget(win, root);

    cuif_widget* knob = cuif_widget_create_knob(100.0f, 45.0f, 90.0f, 110.0f, "Decay", 0.3f, on_knob_change);
    cuif_widget_add_child(root, knob);

    int physical_w = (int)(logical_w * dpi_scale);
    int physical_h = (int)(logical_h * dpi_scale);

    char path1[512];
    snprintf(path1, sizeof(path1), "%s\\offscreen_frame1_before_drag.bmp", out_dir);
    if (!render_and_dump(win, physical_w, physical_h, path1)) {
        printf("FATAL: render_and_dump (frame 1) failed.\n");
        return 1;
    }
    printf("Wrote %s (knob at initial value 0.3).\n", path1);

    /* Inject a real drag on the knob: press at its center, drag upward
     * (cuif's knobs read vertical mouse delta as the value-change gesture),
     * release. Exercises cuif_window_inject_mouse_down/move/up() end to
     * end -- the same functions a future frame-streaming input relay would
     * call from browser-forwarded mouse events. */
    float knob_cx = 100.0f + 45.0f;
    float knob_cy = 45.0f + 55.0f;
    cuif_window_inject_mouse_down(win, knob_cx, knob_cy, 0);
    for (int i = 1; i <= 20; ++i) {
        cuif_window_inject_mouse_move(win, knob_cx, knob_cy - (float)i * 3.0f, 0);
    }
    cuif_window_inject_mouse_up(win, knob_cx, knob_cy - 60.0f, 0);

    printf("Injected a synthetic drag (mouse down + 20 move steps + up) on the knob.\n");

    char path2[512];
    snprintf(path2, sizeof(path2), "%s\\offscreen_frame2_after_drag.bmp", out_dir);
    if (!render_and_dump(win, physical_w, physical_h, path2)) {
        printf("FATAL: render_and_dump (frame 2) failed.\n");
        return 1;
    }
    printf("Wrote %s (knob value should have increased from the drag).\n", path2);

    cuif_widget_destroy(root);
    cuif_window_destroy(win);
    cuif_font_free(cuif_global_font);

    printf("Offscreen harness completed successfully.\n");
    return 0;
}
