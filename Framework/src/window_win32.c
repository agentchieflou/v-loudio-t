#include "cuif/window.h"
#include "cuif/widget.h"

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
    struct cuif_widget* root_widget;
    float last_mx;
    float last_my;

    struct cuif_widget* active_widget;
    struct cuif_widget* hovered_widget;
    struct cuif_widget* open_dropdown;
};

cuif_window* cuif_current_window = NULL;

static const wchar_t* CUIF_WNDCLASS_NAME = L"cuif_window_class";

static LRESULT CALLBACK cuif_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    cuif_window* window = (cuif_window*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!window) return DefWindowProcW(hwnd, msg, wparam, lparam);

    cuif_current_window = window;

    switch (msg) {
        case WM_CLOSE:
            window->should_close = true;
            return 0;
        case WM_LBUTTONDOWN: {
            float mx = (float)((int)(short)LOWORD(lparam));
            float my = (float)((int)(short)HIWORD(lparam));
            window->last_mx = mx;
            window->last_my = my;
            SetCapture(hwnd);
            if (window->root_widget) {
                cuif_widget_dispatch_mouse_down(window->root_widget, mx, my, 0);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            float mx = (float)((int)(short)LOWORD(lparam));
            float my = (float)((int)(short)HIWORD(lparam));
            ReleaseCapture();
            if (window->root_widget) {
                cuif_widget_dispatch_mouse_up(window->root_widget, mx, my, 0);
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            float mx = (float)((int)(short)LOWORD(lparam));
            float my = (float)((int)(short)HIWORD(lparam));
            float dx = mx - window->last_mx;
            float dy = my - window->last_my;
            window->last_mx = mx;
            window->last_my = my;
            if (window->root_widget) {
                cuif_widget_dispatch_mouse_move(window->root_widget, mx, my, dx, dy, (wparam & MK_LBUTTON) ? 0 : -1);
            }
            return 0;
        }
        case WM_SIZE: {
            int w = LOWORD(lparam);
            int h = HIWORD(lparam);
            if (window->root_widget) {
                window->root_widget->w = (float)w;
                window->root_widget->h = (float)h;
            }
            glViewport(0, 0, w, h);
            return 0;
        }
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
    DWORD style = parent ? WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN : WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    int posX = parent ? 0 : CW_USEDEFAULT;
    int posY = parent ? 0 : CW_USEDEFAULT;

    window->hwnd = CreateWindowExW(
        0, CUIF_WNDCLASS_NAME, title_w, style,
        posX, posY, width, height,
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

void cuif_window_set_root_widget(cuif_window* window, struct cuif_widget* root) {
    if (!window) return;
    window->root_widget = root;
    if (root) {
        RECT rect;
        GetClientRect(window->hwnd, &rect);
        root->w = (float)(rect.right - rect.left);
        root->h = (float)(rect.bottom - rect.top);
    }
}

void cuif_window_render_frame(cuif_window* window) {
    if (!window || !window->glrc) return;

    cuif_current_window = window;

    wglMakeCurrent(window->hdc, window->glrc);

    RECT rect;
    GetClientRect(window->hwnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, width, height, 0.0, -1.0, 1.0); /* Y-down coordinate space */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (window->render_fn) {
        window->render_fn(window, window->render_user_data);
    }

    SwapBuffers(window->hdc);
}

void* cuif_window_native_handle(cuif_window* window) {
    return window ? (void*)window->hwnd : NULL;
}

struct cuif_widget* cuif_window_get_active_widget(cuif_window* w) {
    return w ? w->active_widget : NULL;
}

void cuif_window_set_active_widget(cuif_window* w, struct cuif_widget* widget) {
    if (w) w->active_widget = widget;
}

struct cuif_widget* cuif_window_get_hovered_widget(cuif_window* w) {
    return w ? w->hovered_widget : NULL;
}

void cuif_window_set_hovered_widget(cuif_window* w, struct cuif_widget* widget) {
    if (w) w->hovered_widget = widget;
}

struct cuif_widget* cuif_window_get_open_dropdown(cuif_window* w) {
    return w ? w->open_dropdown : NULL;
}

void cuif_window_set_open_dropdown(cuif_window* w, struct cuif_widget* widget) {
    if (w) w->open_dropdown = widget;
}
