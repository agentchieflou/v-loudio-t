#include "cuif/cuif.h"

#include <windows.h>
#include <gl/gl.h>
#include <math.h>
#include <stdio.h>

static float mock_fft_buffer[64];
static cuif_widget* root_container = NULL;

static void update_mock_fft(float time) {
    for (int i = 0; i < 64; ++i) {
        float freq = (float)i / 64.0f;
        float val1 = sinf(freq * 6.0f + time * 3.0f) * 0.4f + 0.4f;
        float val2 = cosf(freq * 15.0f - time * 5.0f) * 0.2f + 0.2f;
        float val = val1 * 0.7f + val2 * 0.3f;
        /* roll off high frequencies */
        val *= (1.0f - freq * 0.6f);
        if (val < 0.05f) val = 0.05f;
        if (val > 0.95f) val = 0.95f;
        mock_fft_buffer[i] = val;
    }
}

static void on_knob_change(cuif_widget* w, float val) {
    (void)w;
    printf("Knob changed: %.3f\n", val);
}

static void on_slider_change(cuif_widget* w, float val) {
    (void)w;
    printf("Slider changed: %.3f\n", val);
}

static void on_button_click(cuif_widget* w) {
    printf("Button clicked! State: %s\n", w->u.button.state ? "ON" : "OFF");
}

static void on_dropdown_change(cuif_widget* w, float val) {
    (void)w;
    printf("Dropdown selected index: %d\n", (int)val);
}

static void on_bezier_change(cuif_widget* w) {
    printf("Bezier curve updated: Nodes -> ");
    for (int i = 0; i < w->u.bezier_editor.node_count; ++i) {
        printf("[%.2f, %.2f] ", w->u.bezier_editor.node_x[i], w->u.bezier_editor.node_y[i]);
    }
    printf("\n");
}

static void on_render(cuif_window* window, void* user_data) {
    (void)window;
    (void)user_data;

    static DWORD start_time = 0;
    if (start_time == 0) start_time = GetTickCount();
    float time = (float)(GetTickCount() - start_time) / 1000.0f;

    update_mock_fft(time);

    glClearColor(0.08f, 0.09f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (root_container) {
        cuif_widget_render(root_container);
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    /* Load system default font */
    cuif_global_font = cuif_font_load("C:\\Windows\\Fonts\\Arial.ttf", 13.0f);
    if (!cuif_global_font) {
        /* fallback to Segoe UI if Arial fails */
        cuif_global_font = cuif_font_load("C:\\Windows\\Fonts\\segoeui.ttf", 13.0f);
    }

    cuif_window_desc desc = {0};
    desc.title = "cuif harness - Full Widget Demo";
    desc.width = 900;
    desc.height = 600;
    desc.parent_native_handle = NULL;

    cuif_window* window = cuif_window_create(&desc);
    if (!window) return 1;

    /* Setup widget layout */
    root_container = cuif_widget_create_container(0, 0, 900, 600);
    cuif_window_set_root_widget(window, root_container);

    /* 1. Knob */
    cuif_widget* knob = cuif_widget_create_knob(50.0f, 50.0f, 90.0f, 110.0f, "Dry/Wet", 0.5f, on_knob_change);
    cuif_widget_add_child(root_container, knob);

    /* 2. Vertical Slider */
    cuif_widget* slider_v = cuif_widget_create_slider(170.0f, 50.0f, 30.0f, 150.0f, true, on_slider_change);
    cuif_widget_add_child(root_container, slider_v);

    /* 3. Horizontal Slider */
    cuif_widget* slider_h = cuif_widget_create_slider(230.0f, 50.0f, 160.0f, 25.0f, false, on_slider_change);
    cuif_widget_add_child(root_container, slider_h);

    /* 4. Button (Toggle) */
    cuif_widget* toggle_btn = cuif_widget_create_button(230.0f, 90.0f, 120.0f, 30.0f, "LFO Sync", true, on_button_click);
    cuif_widget_add_child(root_container, toggle_btn);

    /* 5. Dropdown */
    const char* items[] = {"Room", "Hall", "Plate", "Cathedral", "Spring"};
    cuif_widget* drop = cuif_widget_create_dropdown(230.0f, 135.0f, 150.0f, 25.0f, items, 5, on_dropdown_change);
    cuif_widget_add_child(root_container, drop);

    /* 6. Bezier Curve Editor */
    cuif_widget* bezier = cuif_widget_create_bezier_editor(50.0f, 240.0f, 400.0f, 260.0f, 4, on_bezier_change);
    cuif_widget_add_child(root_container, bezier);

    /* 7. Waveform/FFT Analyzer */
    cuif_widget* analyzer = cuif_widget_create_analyzer(480.0f, 240.0f, 370.0f, 260.0f, mock_fft_buffer, 64, cuif_rgba(0.35f, 0.65f, 0.95f, 1.0f));
    cuif_widget_add_child(root_container, analyzer);

    cuif_window_set_render_callback(window, on_render, NULL);

    while (cuif_window_pump(window)) {
        cuif_window_render_frame(window);
        /* Sleep a bit to limit frame rate (~60fps) */
        Sleep(16);
    }

    cuif_window_destroy(window);
    cuif_widget_destroy(root_container);
    if (cuif_global_font) {
        cuif_font_free(cuif_global_font);
    }

    return 0;
}
