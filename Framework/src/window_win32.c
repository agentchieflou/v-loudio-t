#include "cuif/window.h"
#include "cuif/widget.h"

#include <windows.h>
#include <gl/gl.h>
#include <stdlib.h>
#include <stdio.h>

#define CUIF_LOG(fmt, ...) { \
    char buf[512]; \
    sprintf_s(buf, sizeof(buf), "[CUIF] " fmt "\n", ##__VA_ARGS__); \
    OutputDebugStringA(buf); \
}

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
static int cuif_window_count = 0;
static bool cuif_class_registered = false;

static const wchar_t* CUIF_WNDCLASS_NAME = L"cuif_window_class";

static LRESULT CALLBACK cuif_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    cuif_window* window = (cuif_window*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!window) return DefWindowProcW(hwnd, msg, wparam, lparam);

    cuif_current_window = window;

    switch (msg) {
        case WM_CLOSE:
            window->should_close = true;
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            (void)hdc;
            cuif_window_render_frame(window);
            EndPaint(hwnd, &ps);
            return 0;
        }
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

static bool cuif_register_class_once(HINSTANCE hinstance) {
    if (cuif_class_registered) return true;

    WNDCLASSW wc = {0};
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = cuif_wnd_proc;
    wc.hInstance = hinstance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = CUIF_WNDCLASS_NAME;
    
    if (!RegisterClassW(&wc)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            CUIF_LOG("RegisterClassW failed: error %lu", err);
            return false;
        }
    }

    CUIF_LOG("RegisterClassW succeeded or class already exists.");
    cuif_class_registered = true;
    return true;
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
    if (pixel_format == 0) {
        CUIF_LOG("ChoosePixelFormat failed: error %lu", GetLastError());
        return false;
    }
    if (!SetPixelFormat(window->hdc, pixel_format, &pfd)) {
        CUIF_LOG("SetPixelFormat failed: error %lu", GetLastError());
        return false;
    }

    window->glrc = wglCreateContext(window->hdc);
    if (!window->glrc) {
        CUIF_LOG("wglCreateContext failed: error %lu", GetLastError());
        return false;
    }

    CUIF_LOG("GL Context initialized successfully. glrc = %p", window->glrc);
    return true;
}

cuif_window* cuif_window_create(const cuif_window_desc* desc) {
    cuif_window* window = (cuif_window*)calloc(1, sizeof(cuif_window));
    if (!window) return NULL;

    HMODULE hinstance = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)cuif_window_create, &hinstance);

    CUIF_LOG("cuif_window_create: starting on hinstance = %p", hinstance);

    if (!cuif_register_class_once(hinstance)) {
        free(window);
        return NULL;
    }

    int width = desc->width > 0 ? desc->width : 800;
    int height = desc->height > 0 ? desc->height : 600;

    wchar_t title_w[256] = L"cuif";
    if (desc->title) {
        MultiByteToWideChar(CP_UTF8, 0, desc->title, -1, title_w, 256);
    }

    HWND parent = (HWND)desc->parent_native_handle;
    if (parent) {
        DWORD parent_style = GetWindowLongW(parent, GWL_STYLE);
        CUIF_LOG("Parent window handle = %p. Initial style = %08lX", parent, parent_style);
        if (!(parent_style & WS_CLIPCHILDREN)) {
            CUIF_LOG("Parent window is missing WS_CLIPCHILDREN. Injecting it now.");
            SetWindowLongW(parent, GWL_STYLE, parent_style | WS_CLIPCHILDREN);
            SetWindowPos(parent, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }
    }

    DWORD style = parent ? WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN : WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    int posX = parent ? 0 : CW_USEDEFAULT;
    int posY = parent ? 0 : CW_USEDEFAULT;

    window->hwnd = CreateWindowExW(
        0, CUIF_WNDCLASS_NAME, title_w, style,
        posX, posY, width, height,
        parent, NULL, hinstance, NULL);

    if (!window->hwnd) {
        CUIF_LOG("CreateWindowExW failed: error %lu", GetLastError());
        free(window);
        return NULL;
    }

    CUIF_LOG("CreateWindowExW succeeded. HWND = %p", window->hwnd);

    SetWindowLongPtrW(window->hwnd, GWLP_USERDATA, (LONG_PTR)window);
    window->hdc = GetDC(window->hwnd);

    if (!cuif_init_gl_context(window)) {
        DestroyWindow(window->hwnd);
        free(window);
        return NULL;
    }

    cuif_window_count++;
    CUIF_LOG("Active windows count: %d", cuif_window_count);

    return window;
}

void cuif_window_destroy(cuif_window* window) {
    if (!window) return;

    CUIF_LOG("cuif_window_destroy starting for HWND = %p", window->hwnd);

    if (window->glrc) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(window->glrc);
    }
    if (window->hdc) ReleaseDC(window->hwnd, window->hdc);
    if (window->hwnd) DestroyWindow(window->hwnd);

    free(window);

    cuif_window_count--;
    CUIF_LOG("Active windows count: %d", cuif_window_count);

    if (cuif_window_count == 0) {
        HMODULE hinstance = NULL;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)cuif_window_create, &hinstance);
        CUIF_LOG("Unregistering window class: %S", CUIF_WNDCLASS_NAME);
        UnregisterClassW(CUIF_WNDCLASS_NAME, hinstance);
        cuif_class_registered = false;
    }
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

    if (!wglMakeCurrent(window->hdc, window->glrc)) {
        static int log_counter = 0;
        if (log_counter++ % 60 == 0) {
            CUIF_LOG("wglMakeCurrent failed: error %lu", GetLastError());
        }
        return;
    }

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

    /*
     * Always clear to a defined background color first -- without this,
     * undrawn regions show whatever the swap buffer previously contained
     * (observed as a solid black editor when nothing else painted every
     * pixel). See issue #62.
     */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (window->render_fn) {
        window->render_fn(window, window->render_user_data);
    }

    /*
     * Auto-render the widget tree if one was registered via
     * cuif_window_set_root_widget(). Previously the only way to get pixels
     * on screen was to also call cuif_window_set_render_callback() by hand
     * (as the standalone cuif_harness test does), which the plugin editor
     * never did -- the widget tree was built and wired for input dispatch
     * but never painted. See issue #62.
     */
    if (window->root_widget) {
        cuif_widget_render(window->root_widget);
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
