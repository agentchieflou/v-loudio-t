#ifndef CUIF_WINDOW_H
#define CUIF_WINDOW_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cuif_window cuif_window;

typedef struct {
    const char* title;
    int width;   /* logical pixels */
    int height;  /* logical pixels */

    /*
     * Native parent window handle (HWND on Windows), or NULL for a
     * top-level window. Passing a host-provided handle here is how a
     * plugin editor embeds a cuif window inside its DAW host, per the
     * standard VST3 editor pattern (see Epic 5 / the JUCE<->Framework
     * bridge in Plugins/Reverb/ARCHITECTURE_DECISIONS.md).
     */
    void* parent_native_handle;

    /*
     * Display scale factor (1.0 = no scaling, 2.0 = 200% DPI, etc).
     * width/height above are logical pixels; the actual native window is
     * created at width*dpi_scale x height*dpi_scale physical pixels, so
     * rendering happens at native pixel density on high-DPI displays.
     * <= 0 is treated as 1.0. See cuif_window_set_dpi_scale() for changing
     * this after creation.
     */
    float dpi_scale;
} cuif_window_desc;

typedef void (*cuif_render_fn)(cuif_window* window, void* user_data);

cuif_window* cuif_window_create(const cuif_window_desc* desc);

/*
 * Creates a window that never has a visible on-screen surface: a hidden
 * HWND backs its GL context (reusing the same bootstrap trick as the
 * process-wide root GL context, see window_win32.c), but rendering targets
 * an off-screen FBO instead of that HWND's own framebuffer -- cuif_window_
 * render_frame() renders into it exactly as normal, and cuif_window_
 * read_pixels() reads the result back instead of a real window ever
 * presenting anything via SwapBuffers.
 *
 * Intended for hosts with no native window surface of their own to embed
 * into (e.g. noprod's browser-based UI, which streams rendered frames over
 * a network connection rather than embedding a native child window the way
 * the VST3 host-embedding path does) -- see Plugins/Reverb/
 * ARCHITECTURE_DECISIONS.md and Epic B (LPI) for the fuller rationale.
 *
 * All widget/input/layout code is completely unaware of this distinction --
 * cuif_window_set_root_widget/cuif_window_inject_mouse_*() work identically
 * on an offscreen window as a real one. MSAA is not requested for offscreen
 * windows in this version (FBO multisample-resolve is unimplemented -- see
 * the comment above cuif_init_gl_context's MSAA branch in window_win32.c).
 *
 * logical_width/logical_height and dpi_scale follow the exact same
 * contract as cuif_window_desc's fields.
 */
cuif_window* cuif_window_create_offscreen(int logical_width, int logical_height, float dpi_scale);

/* True if this window was created via cuif_window_create_offscreen(). */
bool cuif_window_is_offscreen(cuif_window* window);

/*
 * Reads back the most recently rendered frame of an offscreen window as
 * tightly-packed RGBA8 (4 bytes/pixel, no padding, row-major, top-left
 * origin), physical_width * physical_height * 4 bytes. Returns false (and
 * leaves out_rgba untouched) if window is NULL, not offscreen, or
 * out_buffer_size is too small -- callers should size their buffer from
 * cuif_window_get_dpi_scale()-derived physical dimensions (or just call
 * once with a generously large buffer computed the same way
 * cuif_window_create_offscreen sized the FBO). No-op on a normal on-screen
 * window; use cuif_window_render_frame() + SwapBuffers (already automatic)
 * for that case instead.
 */
bool cuif_window_read_pixels(cuif_window* window, unsigned char* out_rgba, size_t out_buffer_size);

/*
 * Synthetic input injection -- calls the exact same cuif_widget_dispatch_
 * mouse_*() functions cuif_wnd_proc() calls for real WM_LBUTTONDOWN/UP/
 * WM_MOUSEMOVE messages, just without an actual Win32 message ever being
 * sent. Coordinates are LOGICAL pixels (matching cuif_widget_dispatch_
 * mouse_*()'s own contract -- these are thin wrappers, not a parallel
 * coordinate space). button follows the same convention as the real
 * WM_MOUSEMOVE handler: 0 while a button is held (dragging), -1 while none
 * is. Works on both offscreen and regular windows (a regular window's real
 * WndProc-driven input keeps working unaffected either way -- this is an
 * additional input path, not a replacement).
 */
void cuif_window_inject_mouse_down(cuif_window* window, float logical_x, float logical_y, int button);
void cuif_window_inject_mouse_up(cuif_window* window, float logical_x, float logical_y, int button);
void cuif_window_inject_mouse_move(cuif_window* window, float logical_x, float logical_y, int button);

void cuif_window_destroy(cuif_window* window);

/* Pumps pending OS messages for this window. Returns false once the window has been closed. */
bool cuif_window_pump(cuif_window* window);

void cuif_window_set_render_callback(cuif_window* window, cuif_render_fn fn, void* user_data);
void cuif_window_set_root_widget(cuif_window* window, struct cuif_widget* root);

/* Makes the window's GL context current, invokes the render callback, and swaps buffers. */
void cuif_window_render_frame(cuif_window* window);

void* cuif_window_native_handle(cuif_window* window);

/* Current display scale factor (see cuif_window_desc::dpi_scale). */
float cuif_window_get_dpi_scale(cuif_window* window);

/*
 * Changes the display scale factor after creation (e.g. the window moved to
 * a monitor with different DPI, or the host reported a new scale factor).
 * Resizes the native window to logical_size * scale physical pixels at the
 * window's current logical size. No-op if scale is unchanged.
 */
void cuif_window_set_dpi_scale(cuif_window* window, float scale);

/*
 * Resizes the window to a new logical size (e.g. in response to the host
 * resizing the plugin editor). Converts to physical pixels internally using
 * the window's current dpi_scale -- callers should always pass logical
 * pixels here, never physical ones.
 */
void cuif_window_resize(cuif_window* window, int logical_width, int logical_height);

struct cuif_widget* cuif_window_get_active_widget(cuif_window* w);
void cuif_window_set_active_widget(cuif_window* w, struct cuif_widget* widget);
struct cuif_widget* cuif_window_get_hovered_widget(cuif_window* w);
void cuif_window_set_hovered_widget(cuif_window* w, struct cuif_widget* widget);
struct cuif_widget* cuif_window_get_open_dropdown(cuif_window* w);
void cuif_window_set_open_dropdown(cuif_window* w, struct cuif_widget* widget);

extern cuif_window* cuif_current_window;

#ifdef __cplusplus
}
#endif

#endif /* CUIF_WINDOW_H */
