#include <windows.h>
#include <windowsx.h>
#include <string.h>
#include "libpxl.h"
#include "libpxl_backend.h"

#define EVENT_QUEUE 64
#define WINDOW_CLASS "libpxl"

/* the DPI APIs are loaded dynamically so the backend still runs on older
   systems that predate them */
typedef BOOL (WINAPI *libpxl_set_dpi_ctx_fn)(HANDLE);
typedef BOOL (WINAPI *libpxl_set_dpi_fn)(void);
typedef UINT (WINAPI *libpxl_get_dpi_fn)(void);
typedef BOOL (WINAPI *libpxl_adjust_rect_dpi_fn)(LPRECT, DWORD, BOOL, DWORD, UINT);

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#endif

struct libpxl_backend {
    HWND window;
    int class_registered;
    HDC back_dc;
    HBITMAP back_bmp;
    HBITMAP back_old_bmp;
    int back_width;
    int back_height;
    libpxl_event queue[EVENT_QUEUE];
    int queue_head;
    int queue_tail;
};

static LONGLONG timer_freq;
static const char *backend_error;

static void push_event(libpxl_backend *backend, const libpxl_event *event) {
    int next;
    if (event->type == LIBPXL_MOUSEMOVE && backend->queue_head != backend->queue_tail) {
        int last = (backend->queue_tail + EVENT_QUEUE - 1) % EVENT_QUEUE;
        if (backend->queue[last].type == LIBPXL_MOUSEMOVE) {
            backend->queue[last] = *event; /* keep only the newest position */
            return;
        }
    }
    next = (backend->queue_tail + 1) % EVENT_QUEUE;
    if (next == backend->queue_head) {
        return; /* full: drop */
    }
    backend->queue[backend->queue_tail] = *event;
    backend->queue_tail = next;
}

static int pop_event(libpxl_backend *backend, libpxl_event *event) {
    if (backend->queue_head == backend->queue_tail) {
        return 0;
    }
    *event = backend->queue[backend->queue_head];
    backend->queue_head = (backend->queue_head + 1) % EVENT_QUEUE;
    return 1;
}

static int current_mods(void) {
    int mods = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000) {
        mods |= LIBPXL_MOD_SHIFT;
    }
    if (GetKeyState(VK_CONTROL) & 0x8000) {
        mods |= LIBPXL_MOD_CTRL;
    }
    if (GetKeyState(VK_MENU) & 0x8000) {
        mods |= LIBPXL_MOD_ALT;
    }
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) {
        mods |= LIBPXL_MOD_SUPER;
    }
    return mods;
}

struct libpxl_key_entry {
    int vk;
    int key;
};

static const struct libpxl_key_entry key_table[] = {
    { VK_BACK, LIBPXL_KEY_BACKSPACE },
    { VK_TAB, LIBPXL_KEY_TAB },
    { VK_RETURN, LIBPXL_KEY_ENTER },
    { VK_ESCAPE, LIBPXL_KEY_ESCAPE },
    { VK_SPACE, LIBPXL_KEY_SPACE },
    { VK_DELETE, LIBPXL_KEY_DELETE },
    { VK_INSERT, LIBPXL_KEY_INSERT },
    { VK_HOME, LIBPXL_KEY_HOME },
    { VK_END, LIBPXL_KEY_END },
    { VK_PRIOR, LIBPXL_KEY_PAGEUP },
    { VK_NEXT, LIBPXL_KEY_PAGEDOWN },
    { VK_LEFT, LIBPXL_KEY_LEFT },
    { VK_RIGHT, LIBPXL_KEY_RIGHT },
    { VK_UP, LIBPXL_KEY_UP },
    { VK_DOWN, LIBPXL_KEY_DOWN },
    { VK_F1, LIBPXL_KEY_F1 },
    { VK_F2, LIBPXL_KEY_F2 },
    { VK_F3, LIBPXL_KEY_F3 },
    { VK_F4, LIBPXL_KEY_F4 },
    { VK_F5, LIBPXL_KEY_F5 },
    { VK_F6, LIBPXL_KEY_F6 },
    { VK_F7, LIBPXL_KEY_F7 },
    { VK_F8, LIBPXL_KEY_F8 },
    { VK_F9, LIBPXL_KEY_F9 },
    { VK_F10, LIBPXL_KEY_F10 },
    { VK_F11, LIBPXL_KEY_F11 },
    { VK_F12, LIBPXL_KEY_F12 },
    { VK_OEM_1, ';' },
    { VK_OEM_PLUS, '=' },
    { VK_OEM_COMMA, ',' },
    { VK_OEM_MINUS, '-' },
    { VK_OEM_PERIOD, '.' },
    { VK_OEM_2, '/' },
    { VK_OEM_3, '`' },
    { VK_OEM_4, '[' },
    { VK_OEM_5, '\\' },
    { VK_OEM_6, ']' },
    { VK_OEM_7, '\'' },
};

static int translate_key(WPARAM key, LPARAM lparam) {
    unsigned int scancode = ((unsigned int)lparam >> 16) & 0xff;
    int extended = (int)(((unsigned int)lparam >> 24) & 1);
    size_t i;

    if (key >= '0' && key <= '9') {
        return (int)key;
    }
    if (key >= 'A' && key <= 'Z') {
        return (int)key;
    }
    if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) {
        return (int)('0' + (key - VK_NUMPAD0));
    }
    if (key == VK_MULTIPLY) {
        return '*';
    }
    if (key == VK_ADD) {
        return '+';
    }
    if (key == VK_SUBTRACT) {
        return '-';
    }
    if (key == VK_DECIMAL) {
        return '.';
    }
    if (key == VK_DIVIDE) {
        return '/';
    }
    if (key == VK_SHIFT) {
        return (scancode == 0x36) ? LIBPXL_KEY_RSHIFT : LIBPXL_KEY_LSHIFT;
    }
    if (key == VK_CONTROL) {
        return extended ? LIBPXL_KEY_RCTRL : LIBPXL_KEY_LCTRL;
    }
    if (key == VK_MENU) {
        return extended ? LIBPXL_KEY_RALT : LIBPXL_KEY_LALT;
    }
    if (key == VK_LWIN) {
        return LIBPXL_KEY_LSUPER;
    }
    if (key == VK_RWIN) {
        return LIBPXL_KEY_RSUPER;
    }
    for (i = 0; i < sizeof(key_table) / sizeof(key_table[0]); i++) {
        if ((int)key == key_table[i].vk) {
            return key_table[i].key;
        }
    }
    return LIBPXL_KEY_UNKNOWN;
}

static void backbuffer_destroy(libpxl_backend *backend) {
    if (backend->back_dc) {
        if (backend->back_old_bmp) {
            SelectObject(backend->back_dc, backend->back_old_bmp);
        }
        if (backend->back_bmp) {
            DeleteObject(backend->back_bmp);
        }
        DeleteDC(backend->back_dc);
    }
    backend->back_dc = NULL;
    backend->back_bmp = NULL;
    backend->back_old_bmp = NULL;
    backend->back_width = 0;
    backend->back_height = 0;
}

static int backbuffer_create(libpxl_backend *backend, int width, int height) {
    HDC hdc;
    if (backend->back_dc && backend->back_width == width && backend->back_height == height) {
        return 1;
    }
    backbuffer_destroy(backend);
    hdc = GetDC(backend->window);
    if (!hdc) {
        return 0;
    }
    backend->back_dc = CreateCompatibleDC(hdc);
    backend->back_bmp = CreateCompatibleBitmap(hdc, width, height);
    ReleaseDC(backend->window, hdc);
    if (!backend->back_dc || !backend->back_bmp) {
        backbuffer_destroy(backend);
        return 0;
    }
    backend->back_old_bmp = SelectObject(backend->back_dc, backend->back_bmp);
    backend->back_width = width;
    backend->back_height = height;
    return 1;
}

/* the framebuffer is scaled with nearest neighbors, centered and letterboxed */
static void blit(HDC hdc, libpxl_backend *backend, const uint32_t *pixels, int width, int height) {
    struct {
        BITMAPINFOHEADER header;
        RGBQUAD colors[3];
    } bi;
    RECT client;
    int cw, ch, dw, dh, dx, dy;

    if (!backbuffer_create(backend, width, height)) {
        return;
    }
    memset(&bi, 0, sizeof(bi));
    bi.header.biSize = sizeof(BITMAPINFOHEADER);
    bi.header.biWidth = width;
    bi.header.biHeight = -height;
    bi.header.biPlanes = 1;
    bi.header.biBitCount = 32;
    bi.header.biCompression = BI_BITFIELDS;
    bi.colors[0].rgbRed = bi.colors[1].rgbGreen = bi.colors[2].rgbBlue = 0xff;
    SetDIBitsToDevice(backend->back_dc, 0, 0, width, height, 0, 0, 0, height, (void *)pixels, (BITMAPINFO *)&bi, DIB_RGB_COLORS);

    GetClientRect(backend->window, &client);
    cw = client.right - client.left;
    ch = client.bottom - client.top;
    if (cw <= 0 || ch <= 0) {
        return;
    }
    dw = width;
    dh = height;
    if (cw >= width && ch >= height) {
        int sx = cw / width;
        int sy = ch / height;
        int s = (sx < sy) ? sx : sy;
        if (s > 1) {
            dw = width * s; /* crisp integer scale */
            dh = height * s;
        }
    } else {
        double fx = (double)cw / (double)width;
        double fy = (double)ch / (double)height;
        double f = (fx < fy) ? fx : fy;
        dw = (int)((double)width * f + 0.5);
        dh = (int)((double)height * f + 0.5);
        if (dw < 1) {
            dw = 1;
        }
        if (dh < 1) {
            dh = 1;
        }
    }
    dx = (cw - dw) / 2;
    dy = (ch - dh) / 2;
    if (dx == 0 && dy == 0 && dw == cw && dh == ch) {
        BitBlt(hdc, 0, 0, width, height, backend->back_dc, 0, 0, SRCCOPY);
    } else {
        PatBlt(hdc, 0, 0, cw, ch, BLACKNESS); /* letterbox */
        SetStretchBltMode(hdc, COLORONCOLOR);
        StretchBlt(hdc, dx, dy, dw, dh, backend->back_dc, 0, 0, width, height, SRCCOPY);
    }
}

static void push_mouse_button(libpxl_backend *backend, libpxl_event_type type, int button, int x, int y, int mods) {
    libpxl_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.button = button;
    event.x = x;
    event.y = y;
    event.mods = mods;
    push_event(backend, &event);
}

static void push_key(libpxl_backend *backend, libpxl_event_type type, int key, int mods, int repeat) {
    libpxl_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.key = key;
    event.mods = mods;
    event.repeat = repeat;
    push_event(backend, &event);
}

static void push_resize(libpxl_backend *backend, int width, int height) {
    libpxl_event event;
    memset(&event, 0, sizeof(event));
    event.type = LIBPXL_RESIZE;
    event.x = width;
    event.y = height;
    push_event(backend, &event);
}

LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    libpxl_backend *backend = (libpxl_backend *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!backend) {
        return DefWindowProc(hwnd, msg, wParam, lParam); /* during creation */
    }
    switch (msg) {
        case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                const uint32_t *pixels = libpxl_pixels();
                if (pixels) {
                    blit(hdc, backend, pixels, libpxl_width(), libpxl_height());
                }
                EndPaint(hwnd, &ps);
            }
            return 0;
        case WM_ERASEBKGND:
            return 1; /* the framebuffer covers the client area */
        case WM_SIZE: {
            int cw = LOWORD(lParam);
            int ch = HIWORD(lParam);
            if (cw > 0 && ch > 0) {
                push_resize(backend, cw, ch);
            }
        }
            return 0;
        case WM_KEYDOWN:
        case WM_KEYUP: {
            int repeat = (msg == WM_KEYDOWN) ? (int)(((unsigned int)lParam >> 30) & 1) : 0;
            push_key(backend, (msg == WM_KEYDOWN) ? LIBPXL_KEYDOWN : LIBPXL_KEYUP,
                     translate_key(wParam, lParam), current_mods(), repeat);
        }
            return 0;
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP: {
            int repeat = (msg == WM_SYSKEYDOWN) ? (int)(((unsigned int)lParam >> 30) & 1) : 0;
            push_key(backend, (msg == WM_SYSKEYDOWN) ? LIBPXL_KEYDOWN : LIBPXL_KEYUP,
                     translate_key(wParam, lParam), current_mods(), repeat);
        }
            return DefWindowProc(hwnd, msg, wParam, lParam); /* keep Alt+F4 working */
        case WM_LBUTTONDOWN:
            push_mouse_button(backend, LIBPXL_MOUSEDOWN, 1, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), current_mods());
            return 0;
        case WM_LBUTTONUP:
            push_mouse_button(backend, LIBPXL_MOUSEUP, 1, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), current_mods());
            return 0;
        case WM_MBUTTONDOWN:
            push_mouse_button(backend, LIBPXL_MOUSEDOWN, 2, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), current_mods());
            return 0;
        case WM_MBUTTONUP:
            push_mouse_button(backend, LIBPXL_MOUSEUP, 2, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), current_mods());
            return 0;
        case WM_RBUTTONDOWN:
            push_mouse_button(backend, LIBPXL_MOUSEDOWN, 3, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), current_mods());
            return 0;
        case WM_RBUTTONUP:
            push_mouse_button(backend, LIBPXL_MOUSEUP, 3, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), current_mods());
            return 0;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
            push_mouse_button(backend,
                               (msg == WM_XBUTTONDOWN) ? LIBPXL_MOUSEDOWN : LIBPXL_MOUSEUP,
                               (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? 4 : 5,
                               GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), current_mods());
            return TRUE;
        case WM_MOUSEMOVE:
            push_mouse_button(backend, LIBPXL_MOUSEMOVE, 0, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), current_mods());
            return 0;
        case WM_MOUSEWHEEL: {
            libpxl_event event;
            POINT point;
            int delta = (int)GET_WHEEL_DELTA_WPARAM(wParam);
            point.x = GET_X_LPARAM(lParam);
            point.y = GET_Y_LPARAM(lParam);
            ScreenToClient(hwnd, &point);
            memset(&event, 0, sizeof(event));
            event.type = LIBPXL_MOUSEWHEEL;
            event.x = point.x;
            event.y = point.y;
            event.wheel = delta / WHEEL_DELTA;
            if (event.wheel == 0 && delta != 0) {
                event.wheel = (delta > 0) ? 1 : -1; /* fractional deltas */
            }
            event.mods = current_mods();
            push_event(backend, &event);
        }
            return 0;
        case WM_SETFOCUS:
            push_key(backend, LIBPXL_FOCUSGAINED, 0, current_mods(), 0);
            return 0;
        case WM_KILLFOCUS:
            push_key(backend, LIBPXL_FOCUSLOST, 0, 0, 0);
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

static void enable_dpi_awareness(void) {
    HMODULE user32 = GetModuleHandleA("user32.dll");
    libpxl_set_dpi_ctx_fn set_ctx;
    libpxl_set_dpi_fn set_dpi;
    if (!user32) {
        return;
    }
    set_ctx = (libpxl_set_dpi_ctx_fn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if (set_ctx) {
        if (set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            return;
        }
    }
    set_dpi = (libpxl_set_dpi_fn)GetProcAddress(user32, "SetProcessDPIAware");
    if (set_dpi) {
        set_dpi();
    }
}

static UINT system_dpi(void) {
    HMODULE user32 = GetModuleHandleA("user32.dll");
    HDC hdc;
    if (user32) {
        libpxl_get_dpi_fn get_dpi =
            (libpxl_get_dpi_fn)GetProcAddress(user32, "GetDpiForSystem");
        if (get_dpi) {
            UINT dpi = get_dpi();
            if (dpi) {
                return dpi;
            }
        }
    }
    hdc = GetDC(NULL);
    if (hdc) {
        int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(NULL, hdc);
        if (dpi > 0) {
            return (UINT)dpi;
        }
    }
    return 96;
}

/* the window is created from the client size, so the framebuffer maps 1:1 */
static int window_rect_for_client(int width, int height, RECT *rect) {
    HMODULE user32 = GetModuleHandleA("user32.dll");
    rect->left = 0;
    rect->top = 0;
    rect->right = width;
    rect->bottom = height;
    if (user32) {
        libpxl_adjust_rect_dpi_fn adjust_dpi =
            (libpxl_adjust_rect_dpi_fn)GetProcAddress(user32, "AdjustWindowRectExForDpi");
        if (adjust_dpi) {
            return adjust_dpi(rect, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_CLIENTEDGE, system_dpi()) ? 1 : 0;
        }
    }
    return AdjustWindowRectEx(rect, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_CLIENTEDGE) ? 1 : 0;
}

libpxl_backend *libpxl_backend_open(const char *title, int width, int height) {
    libpxl_backend *backend;
    HINSTANCE instance;
    WNDCLASSEX wc;
    RECT rect;
    MSG msg;
    LARGE_INTEGER freq;

    backend_error = NULL;
    backend = (libpxl_backend *)malloc(sizeof(*backend));
    if (!backend) {
        backend_error = "libpxl: out of memory";
        return NULL;
    }
    memset(backend, 0, sizeof(*backend));

    enable_dpi_awareness();

    instance = GetModuleHandle(NULL);
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_VREDRAW | CS_HREDRAW;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = instance;
    wc.lpszClassName = WINDOW_CLASS;
    if (RegisterClassEx(&wc)) {
        backend->class_registered = 1;
    } else if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        backend_error = "libpxl: RegisterClassEx failed";
        free(backend);
        return NULL;
    }

    /* drop anything left over from a previous window */
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
    }

    if (!window_rect_for_client(width, height, &rect)) {
        backend_error = "libpxl: AdjustWindowRect failed";
        libpxl_backend_close(backend);
        return NULL;
    }
    backend->window = CreateWindowEx(WS_EX_CLIENTEDGE, WINDOW_CLASS, title,
                                    WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                    rect.right - rect.left, rect.bottom - rect.top,
                                    NULL, NULL, instance, NULL);
    if (!backend->window) {
        backend_error = "libpxl: CreateWindowEx failed";
        libpxl_backend_close(backend);
        return NULL;
    }
    SetWindowLongPtr(backend->window, GWLP_USERDATA, (LONG_PTR)backend);
    if (!QueryPerformanceFrequency(&freq)) {
        timer_freq = 0;
    } else {
        timer_freq = freq.QuadPart;
    }
    ShowWindow(backend->window, SW_NORMAL);
    UpdateWindow(backend->window);
    return backend;
}

void libpxl_backend_close(libpxl_backend *backend) {
    if (!backend) {
        return;
    }
    backbuffer_destroy(backend);
    if (backend->window) {
        DestroyWindow(backend->window);
        backend->window = NULL;
    }
    if (backend->class_registered) {
        UnregisterClass(WINDOW_CLASS, GetModuleHandle(NULL));
        backend->class_registered = 0;
    }
    free(backend);
}

void libpxl_backend_present(libpxl_backend *backend, const uint32_t *pixels, int width, int height) {
    HDC hdc;
    if (!backend || !backend->window || !pixels) {
        return;
    }
    hdc = GetDC(backend->window);
    if (hdc) {
        blit(hdc, backend, pixels, width, height);
        ReleaseDC(backend->window, hdc);
    }
}

int libpxl_backend_poll(libpxl_backend *backend, libpxl_event *event) {
    MSG msg;
    for (;;) {
        if (pop_event(backend, event)) {
            return 1;
        }
        if (!PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            return 0;
        }
        if (msg.message == WM_QUIT) {
            libpxl_event quit;
            memset(&quit, 0, sizeof(quit));
            quit.type = LIBPXL_QUIT;
            push_event(backend, &quit);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void libpxl_backend_delay(int ms) {
    if (ms > 0) {
        Sleep((DWORD)ms);
    }
}

uint64_t libpxl_backend_time(void) {
    LARGE_INTEGER counter;
    if (timer_freq == 0) {
        LARGE_INTEGER freq;
        if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0) {
            return 0;
        }
        timer_freq = freq.QuadPart;
    }
    if (!QueryPerformanceCounter(&counter)) {
        return 0;
    }
    return (uint64_t)(counter.QuadPart * 1000 / timer_freq);
}

const char *libpxl_backend_error(void) {
    return backend_error;
}
