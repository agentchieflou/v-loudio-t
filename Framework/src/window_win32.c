#include "cuif/window.h"
#include "cuif/widget.h"
#include "cuif/theme.h"
#include "cuif/cuif_dpi_utils.h"
#include "cuif/font.h"

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

    float dpi_scale;  /* always > 0; 1.0 = no scaling */
    int logical_w;
    int logical_h;

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
            /*
             * lparam is always physical/device pixels (Win32 window messages
             * don't know about our logical coordinate space) -- widgets are
             * laid out in logical pixels, so convert before dispatching.
             */
            float mx = cuif_physical_to_logical_px((float)((int)(short)LOWORD(lparam)), window->dpi_scale);
            float my = cuif_physical_to_logical_px((float)((int)(short)HIWORD(lparam)), window->dpi_scale);
            window->last_mx = mx;
            window->last_my = my;
            SetCapture(hwnd);
            if (window->root_widget) {
                cuif_widget_dispatch_mouse_down(window->root_widget, mx, my, 0);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            float mx = cuif_physical_to_logical_px((float)((int)(short)LOWORD(lparam)), window->dpi_scale);
            float my = cuif_physical_to_logical_px((float)((int)(short)HIWORD(lparam)), window->dpi_scale);
            ReleaseCapture();
            if (window->root_widget) {
                cuif_widget_dispatch_mouse_up(window->root_widget, mx, my, 0);
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            float mx = cuif_physical_to_logical_px((float)((int)(short)LOWORD(lparam)), window->dpi_scale);
            float my = cuif_physical_to_logical_px((float)((int)(short)HIWORD(lparam)), window->dpi_scale);
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
            /* lparam here is also physical pixels -- track both for the projection/viewport split in render_frame. */
            int physical_w = LOWORD(lparam);
            int physical_h = HIWORD(lparam);
            window->logical_w = (int)cuif_physical_to_logical_px((float)physical_w, window->dpi_scale);
            window->logical_h = (int)cuif_physical_to_logical_px((float)physical_h, window->dpi_scale);
            if (window->root_widget) {
                window->root_widget->w = (float)window->logical_w;
                window->root_widget->h = (float)window->logical_h;
            }
            glViewport(0, 0, physical_w, physical_h);
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

/*
 * MSAA support (#80). GL/wglext.h isn't vendored in this environment, so
 * the WGL_ARB_multisample constants and the one function pointer type we
 * need are defined locally rather than pulling in a GL loader dependency
 * for this alone -- standard practice for a single-extension use case.
 */
#ifndef WGL_DRAW_TO_WINDOW_ARB
#define WGL_DRAW_TO_WINDOW_ARB 0x2001
#define WGL_SUPPORT_OPENGL_ARB 0x2010
#define WGL_DOUBLE_BUFFER_ARB  0x2011
#define WGL_PIXEL_TYPE_ARB     0x2013
#define WGL_TYPE_RGBA_ARB      0x202B
#define WGL_COLOR_BITS_ARB     0x2014
#define WGL_DEPTH_BITS_ARB     0x2022
#define WGL_STENCIL_BITS_ARB   0x2023
#define WGL_SAMPLE_BUFFERS_ARB 0x2041
#define WGL_SAMPLES_ARB        0x2042
#endif
#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D /* not in the GL 1.1-era gl/gl.h Windows ships */
#endif

typedef BOOL(WINAPI* PFNCUIFWGLCHOOSEPIXELFORMATARBPROC)(HDC hdc, const int* piAttribIList, const FLOAT* pfAttribFList, UINT nMaxFormats, int* piFormats, UINT* nNumFormats);

#define CUIF_MSAA_SAMPLES 4

/* Resolved once per process (mirrors the cuif_class_registered pattern below) -- this plugin can have several simultaneous instances, each calling cuif_window_create, and the dummy-context bootstrap this requires must not repeat per instance. */
static bool cuif_wgl_ext_probed = false;
static PFNCUIFWGLCHOOSEPIXELFORMATARBPROC cuif_wglChoosePixelFormatARB = NULL;

/*
 * Process-wide root GL context (#91, closes #88). Every per-window HGLRC
 * created in cuif_init_gl_context() shares its object namespace (textures,
 * display lists) with this one via wglShareLists(), so GL objects baked
 * once (e.g. the font atlas in font.c) stay valid across every window's
 * context for as long as ANY window exists -- including across a single
 * window closing and reopening, where previously the font texture lived
 * only in that one now-destroyed context.
 *
 * Bootstrapped from the same hidden-dummy-window trick already used to
 * resolve wglChoosePixelFormatARB below, except this context is kept
 * alive (not torn down at the end of the probe) and reused as the share
 * root for the lifetime of the process. Lazily created on the first ever
 * cuif_window_create() call; torn down in cuif_window_destroy() once
 * cuif_window_count returns to zero (see that function).
 */
static HWND cuif_root_hwnd = NULL;
static HDC cuif_root_hdc = NULL;
static HGLRC cuif_root_glrc = NULL;

static void cuif_ensure_gl_root_context(void) {
    if (cuif_wgl_ext_probed) return;
    cuif_wgl_ext_probed = true; /* only ever attempt this once per lazy-create cycle, success or not */

    HINSTANCE hinstance = GetModuleHandleW(NULL);

    WNDCLASSW dummy_wc = {0};
    dummy_wc.lpfnWndProc = DefWindowProcW;
    dummy_wc.hInstance = hinstance;
    dummy_wc.lpszClassName = L"cuif_wgl_probe_class";
    if (!RegisterClassW(&dummy_wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        CUIF_LOG("GL root context: RegisterClassW for dummy window failed: error %lu", GetLastError());
        return;
    }

    cuif_root_hwnd = CreateWindowExW(0, dummy_wc.lpszClassName, L"", 0, 0, 0, 1, 1, NULL, NULL, hinstance, NULL);
    if (!cuif_root_hwnd) {
        CUIF_LOG("GL root context: dummy CreateWindowExW failed: error %lu", GetLastError());
        UnregisterClassW(dummy_wc.lpszClassName, hinstance);
        return;
    }

    cuif_root_hdc = GetDC(cuif_root_hwnd);
    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;

    int dummy_pf = ChoosePixelFormat(cuif_root_hdc, &pfd);
    if (dummy_pf != 0 && SetPixelFormat(cuif_root_hdc, dummy_pf, &pfd)) {
        cuif_root_glrc = wglCreateContext(cuif_root_hdc);
        if (cuif_root_glrc) {
            /* wglGetProcAddress only resolves valid pointers with a context current. */
            if (wglMakeCurrent(cuif_root_hdc, cuif_root_glrc)) {
                cuif_wglChoosePixelFormatARB = (PFNCUIFWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
                wglMakeCurrent(NULL, NULL);
                CUIF_LOG("GL root context: created glrc = %p, wglChoosePixelFormatARB = %p", (void*)cuif_root_glrc, (void*)cuif_wglChoosePixelFormatARB);
            }
        }
    } else {
        CUIF_LOG("GL root context: dummy pixel format setup failed: error %lu", GetLastError());
    }

    if (!cuif_root_glrc) {
        /* Root context creation failed -- clean up the dummy window/HDC so we
         * don't leak it, but leave the statics NULL. Every per-window context
         * still gets created normally in cuif_init_gl_context(); it just won't
         * have anything to share with (logged there, non-fatal). */
        if (cuif_root_hdc) { ReleaseDC(cuif_root_hwnd, cuif_root_hdc); cuif_root_hdc = NULL; }
        DestroyWindow(cuif_root_hwnd);
        cuif_root_hwnd = NULL;
        UnregisterClassW(dummy_wc.lpszClassName, hinstance);
    }
}

static bool cuif_init_gl_context(cuif_window* window) {
    cuif_ensure_gl_root_context();

    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pixel_format = 0;

    if (cuif_wglChoosePixelFormatARB) {
        int attribs[] = {
            WGL_DRAW_TO_WINDOW_ARB, TRUE,
            WGL_SUPPORT_OPENGL_ARB, TRUE,
            WGL_DOUBLE_BUFFER_ARB, TRUE,
            WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
            WGL_COLOR_BITS_ARB, 32,
            WGL_DEPTH_BITS_ARB, 24,
            WGL_STENCIL_BITS_ARB, 8,
            WGL_SAMPLE_BUFFERS_ARB, 1,
            WGL_SAMPLES_ARB, CUIF_MSAA_SAMPLES,
            0
        };
        UINT num_formats = 0;
        if (cuif_wglChoosePixelFormatARB(window->hdc, attribs, NULL, 1, &pixel_format, &num_formats) && num_formats > 0) {
            CUIF_LOG("Resolved a %dx MSAA pixel format via WGL_ARB_multisample.", CUIF_MSAA_SAMPLES);
        } else {
            CUIF_LOG("wglChoosePixelFormatARB found no MSAA-capable format; falling back to the plain pixel format.");
            pixel_format = 0;
        }
    }

    if (pixel_format == 0) {
        pixel_format = ChoosePixelFormat(window->hdc, &pfd);
        if (pixel_format == 0) {
            CUIF_LOG("ChoosePixelFormat failed: error %lu", GetLastError());
            return false;
        }
    }

    /*
     * The PFD passed here is descriptive only once pixel_format was chosen
     * via the ARB path above -- Windows uses the format index, not this
     * struct's contents, in that case. Reusing the plain pfd we already
     * built is the standard pattern (matches e.g. the well-known NeHe/
     * arcsynthesis WGL multisample tutorials).
     */
    if (!SetPixelFormat(window->hdc, pixel_format, &pfd)) {
        CUIF_LOG("SetPixelFormat failed: error %lu", GetLastError());
        return false;
    }

    window->glrc = wglCreateContext(window->hdc);
    if (!window->glrc) {
        CUIF_LOG("wglCreateContext failed: error %lu", GetLastError());
        return false;
    }

    /*
     * Share GL object namespace (textures, display lists) with the
     * process-wide root context (#91, closes #88) so objects baked in one
     * window's context (the font atlas, in particular) stay valid in every
     * other window's context, including a window created later. Logged and
     * non-fatal on failure -- that window just falls back to today's
     * unshared behavior rather than failing to open at all.
     */
    if (cuif_root_glrc && !wglShareLists(cuif_root_glrc, window->glrc)) {
        CUIF_LOG("wglShareLists failed: error %lu (this window will render unshared GL objects)", GetLastError());
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

    int logical_width = desc->width > 0 ? desc->width : 800;
    int logical_height = desc->height > 0 ? desc->height : 600;
    float dpi_scale = desc->dpi_scale > 0.0f ? desc->dpi_scale : 1.0f;
    int physical_width = cuif_logical_to_physical_px(logical_width, dpi_scale);
    int physical_height = cuif_logical_to_physical_px(logical_height, dpi_scale);

    window->dpi_scale = dpi_scale;
    window->logical_w = logical_width;
    window->logical_h = logical_height;

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
        posX, posY, physical_width, physical_height,
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

    /*
     * Make the context current immediately, not just on the first
     * cuif_window_render_frame() call. Without this, any GL work done by
     * the caller between cuif_window_create() returning and the first
     * render (e.g. cuif_font_load()'s texture upload) runs with no
     * context current at all -- undefined behavior that happened to be
     * silently tolerated by this driver rather than a guarantee.
     */
    if (!wglMakeCurrent(window->hdc, window->glrc)) {
        CUIF_LOG("wglMakeCurrent failed immediately after context creation: error %lu", GetLastError());
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

        /*
         * Free the process-wide font atlas (#92, fixes reopen-in-place
         * fuzzy/blank text) now that this was the last live window. The
         * window whose destruction brought the count to zero already had
         * its own context deleted and made non-current above, so there is
         * currently NO context current -- glDeleteTextures() inside
         * cuif_font_free() needs one. The root context still exists at this
         * point (torn down below) and shares the same object namespace the
         * texture was created in, so make it current just for this call.
         *
         * cuif_global_font is only ever alive while at least one window
         * exists anywhere in the process -- the reopen-in-place case
         * (single track, close, reopen) is exactly "count hits 0, then back
         * to 1", which now correctly re-triggers cuif_font_load() on the
         * next window creation instead of reusing a pointer whose
         * texture_id no longer exists in any live context.
         */
        if (cuif_global_font) {
            if (cuif_root_glrc) {
                wglMakeCurrent(cuif_root_hdc, cuif_root_glrc);
            }
            cuif_font_free(cuif_global_font);
            cuif_global_font = NULL;
            wglMakeCurrent(NULL, NULL);
        }

        /*
         * Tear down the process-wide root context (#91) now that no window
         * anywhere in the process needs its shared GL objects anymore. Reset
         * cuif_wgl_ext_probed too so a later cold start (count back up from
         * zero) re-probes and re-creates a fresh root context/dummy window
         * rather than silently reusing now-dangling handles.
         */
        if (cuif_root_glrc) {
            wglMakeCurrent(NULL, NULL);
            wglDeleteContext(cuif_root_glrc);
            cuif_root_glrc = NULL;
        }
        if (cuif_root_hdc) {
            ReleaseDC(cuif_root_hwnd, cuif_root_hdc);
            cuif_root_hdc = NULL;
        }
        if (cuif_root_hwnd) {
            DestroyWindow(cuif_root_hwnd);
            cuif_root_hwnd = NULL;
        }
        UnregisterClassW(L"cuif_wgl_probe_class", hinstance);
        cuif_wgl_ext_probed = false;
        cuif_wglChoosePixelFormatARB = NULL;
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
        root->w = (float)window->logical_w;
        root->h = (float)window->logical_h;
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
    int physical_width = rect.right - rect.left;
    int physical_height = rect.bottom - rect.top;

    /*
     * Viewport covers the full physical (device-pixel) framebuffer, but the
     * projection stays in logical units -- this is what makes rendering
     * happen at native pixel density on high-DPI displays without touching
     * any widget layout/hit-test coordinates, which are all logical.
     */
    glViewport(0, 0, physical_width, physical_height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, window->logical_w, window->logical_h, 0.0, -1.0, 1.0); /* Y-down coordinate space */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    /* No-op if the pixel format has no multisample buffers (MSAA unavailable/fallback path). */
    glEnable(GL_MULTISAMPLE);

    /*
     * Always clear to a defined background color first -- without this,
     * undrawn regions show whatever the swap buffer previously contained
     * (observed as a solid black editor when nothing else painted every
     * pixel). See issue #62. Reads from the active theme (rather than a
     * hardcoded color) so non-default themes (#66, #67) actually show
     * their background outside the bezier/analyzer graph boxes.
     */
    cuif_color bg = cuif_get_theme()->background;
    glClearColor(bg.r, bg.g, bg.b, bg.a);
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

float cuif_window_get_dpi_scale(cuif_window* window) {
    return window ? window->dpi_scale : 1.0f;
}

void cuif_window_set_dpi_scale(cuif_window* window, float scale) {
    if (!window) return;
    if (scale <= 0.0f) scale = 1.0f;
    if (scale == window->dpi_scale) return;

    window->dpi_scale = scale;
    cuif_window_resize(window, window->logical_w, window->logical_h);
}

void cuif_window_resize(cuif_window* window, int logical_width, int logical_height) {
    if (!window) return;

    window->logical_w = logical_width;
    window->logical_h = logical_height;

    int physical_width = cuif_logical_to_physical_px(logical_width, window->dpi_scale);
    int physical_height = cuif_logical_to_physical_px(logical_height, window->dpi_scale);

    /* SWP_NOMOVE: resize only, never touch position -- correct for both a
     * child window already positioned by its owner and a top-level window
     * the user has moved. */
    SetWindowPos(window->hwnd, NULL, 0, 0, physical_width, physical_height, SWP_NOMOVE | SWP_NOZORDER);

    if (window->root_widget) {
        window->root_widget->w = (float)logical_width;
        window->root_widget->h = (float)logical_height;
    }
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
