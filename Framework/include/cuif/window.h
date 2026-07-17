#ifndef CUIF_WINDOW_H
#define CUIF_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

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
