#include "cuif/cuif.h"

#include <windows.h>
#include <gl/gl.h>

/*
 * Standalone test harness for the cuif framework -- no JUCE/plugin
 * dependency. Opens a native window and clears it every frame, proving the
 * Win32 + WGL pipeline works end to end. Widget exercises get added here as
 * Epic 1's individual widgets land (see issues #11-#18).
 */

static void on_render(cuif_window* window, void* user_data) {
    (void)window;
    (void)user_data;
    glClearColor(0.11f, 0.12f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    cuif_window_desc desc = {0};
    desc.title = "cuif harness";
    desc.width = 900;
    desc.height = 600;
    desc.parent_native_handle = NULL;

    cuif_window* window = cuif_window_create(&desc);
    if (!window) return 1;

    cuif_window_set_render_callback(window, on_render, NULL);

    while (cuif_window_pump(window)) {
        cuif_window_render_frame(window);
    }

    cuif_window_destroy(window);
    return 0;
}
