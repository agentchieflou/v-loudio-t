#include "cuif/window.h"

#include <windows.h>
#include <gl/gl.h>
#include <stdlib.h>

struct cuif_window {
    HWND hwnd;
    HDC hdc;
    HGLRC glrc;
    bool should_close;
    cuif_render_fn render_fn;
    void* render_user_data;
};

static const wchar_t* CUIF_WNDCLASS_NAME = L"cuif_window_class";

static LRESULT CALLBACK cuif_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    cuif_window* window = (cuif_window*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CLOSE:
            if (window) window->should_close = true;
            return 0;
        case WM_DESTROY:
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

static void cuif_register_class_once(HINSTANCE hinstance) {
    static bool registered = false;
    if (registered) return;

    WNDCLASSW wc = {0};
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = cuif_wnd_proc;
    wc.hInstance = hinstance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = CUIF_WNDCLASS_NAME;
    RegisterClassW(&wc);

    registered = true;
}

static bool cuif_init_gl_context(cuif_window* window) {
    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pixel_format = ChoosePixelFormat(window->hdc, &pfd);
    if (pixel_format == 0) return false;
    if (!SetPixelFormat(window->hdc, pixel_format, &pfd)) return false;

    window->glrc = wglCreateContext(window->hdc);
    if (!window->glrc) return false;

    return true;
}

cuif_window* cuif_window_create(const cuif_window_desc* desc) {
    cuif_window* window = (cuif_window*)calloc(1, sizeof(cuif_window));
    if (!window) return NULL;

    HINSTANCE hinstance = GetModuleHandleW(NULL);
    cuif_register_class_once(hinstance);

    int width = desc->width > 0 ? desc->width : 800;
    int height = desc->height > 0 ? desc->height : 600;

    wchar_t title_w[256] = L"cuif";
    if (desc->title) {
        MultiByteToWideChar(CP_UTF8, 0, desc->title, -1, title_w, 256);
    }

    HWND parent = (HWND)desc->parent_native_handle;
    DWORD style = parent ? WS_CHILD | WS_VISIBLE : WS_OVERLAPPEDWINDOW | WS_VISIBLE;

    window->hwnd = CreateWindowExW(
        0, CUIF_WNDCLASS_NAME, title_w, style,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        parent, NULL, hinstance, NULL);

    if (!window->hwnd) {
        free(window);
        return NULL;
    }

    SetWindowLongPtrW(window->hwnd, GWLP_USERDATA, (LONG_PTR)window);
    window->hdc = GetDC(window->hwnd);

    if (!cuif_init_gl_context(window)) {
        DestroyWindow(window->hwnd);
        free(window);
        return NULL;
    }

    return window;
}

void cuif_window_destroy(cuif_window* window) {
    if (!window) return;

    if (window->glrc) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(window->glrc);
    }
    if (window->hdc) ReleaseDC(window->hwnd, window->hdc);
    if (window->hwnd) DestroyWindow(window->hwnd);

    free(window);
}

bool cuif_window_pump(cuif_window* window) {
    if (!window) return false;

    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return !window->should_close;
}

void cuif_window_set_render_callback(cuif_window* window, cuif_render_fn fn, void* user_data) {
    if (!window) return;
    window->render_fn = fn;
    window->render_user_data = user_data;
}

void cuif_window_render_frame(cuif_window* window) {
    if (!window || !window->glrc) return;

    wglMakeCurrent(window->hdc, window->glrc);

    if (window->render_fn) {
        window->render_fn(window, window->render_user_data);
    }

    SwapBuffers(window->hdc);
}

void* cuif_window_native_handle(cuif_window* window) {
    return window ? (void*)window->hwnd : NULL;
}
