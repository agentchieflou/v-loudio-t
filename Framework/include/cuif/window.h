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
    int width;
    int height;

    /*
     * Native parent window handle (HWND on Windows), or NULL for a
     * top-level window. Passing a host-provided handle here is how a
     * plugin editor embeds a cuif window inside its DAW host, per the
     * standard VST3 editor pattern (see Epic 5 / the JUCE<->Framework
     * bridge in Plugins/Reverb/ARCHITECTURE_DECISIONS.md).
     */
    void* parent_native_handle;
} cuif_window_desc;

typedef void (*cuif_render_fn)(cuif_window* window, void* user_data);

cuif_window* cuif_window_create(const cuif_window_desc* desc);
void cuif_window_destroy(cuif_window* window);

/* Pumps pending OS messages for this window. Returns false once the window has been closed. */
bool cuif_window_pump(cuif_window* window);

void cuif_window_set_render_callback(cuif_window* window, cuif_render_fn fn, void* user_data);

/* Makes the window's GL context current, invokes the render callback, and swaps buffers. */
void cuif_window_render_frame(cuif_window* window);

void* cuif_window_native_handle(cuif_window* window);

#ifdef __cplusplus
}
#endif

#endif /* CUIF_WINDOW_H */
