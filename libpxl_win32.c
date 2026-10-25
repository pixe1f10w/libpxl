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

static HWND window;
static int class_registered;
static HDC back_dc;
static HBITMAP back_bmp;
static HBITMAP back_old_bmp;
static int back_width;
static int back_height;
static LONGLONG timer_freq;
static libpxl_event queue[EVENT_QUEUE];
static int queue_head;
static int queue_tail;

static void push_event(const libpxl_event *event) {
    int next;
    if (event->type == LIBPXL_MOUSEMOVE && queue_head != queue_tail) {
        int last = (queue_tail + EVENT_QUEUE - 1) % EVENT_QUEUE;
        if (queue[last].type == LIBPXL_MOUSEMOVE) {
            queue[last] = *event; /* keep only the newest position */
            return;
        }
    }
    next = (queue_tail + 1) % EVENT_QUEUE;
    if (next == queue_head) {
        return; /* full: drop */
    }
    queue[queue_tail] = *event;
    queue_tail = next;
}

static int pop_event(libpxl_event *event) {
    if (queue_head == queue_tail) {
        return 0;
    }
    *event = queue[queue_head];
    queue_head = (queue_head + 1) % EVENT_QUEUE;
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

static void backbuffer_destroy(void) {
    if (back_dc) {
        if (back_old_bmp) {
            SelectObject(back_dc, back_old_bmp);
        }
        if (back_bmp) {
            DeleteObject(back_bmp);
        }
        DeleteDC(back_dc);
    }
    back_dc = NULL;
    back_bmp = NULL;
    back_old_bmp = NULL;
    back_width = 0;
    back_height = 0;
}

static int backbuffer_create(int width, int height) {
    HDC hdc;
    if (back_dc && back_width == width && back_height == height) {
        return 1;
    }
    backbuffer_destroy();
    hdc = GetDC(window);
    if (!hdc) {
        return 0;
    }
    back_dc = CreateCompatibleDC(hdc);
    back_bmp = CreateCompatibleBitmap(hdc, width, height);
    ReleaseDC(window, hdc);
    if (!back_dc || !back_bmp) {
        backbuffer_destroy();
        return 0;
    }
    back_old_bmp = SelectObject(back_dc, back_bmp);
    back_width = width;
    back_height = height;
    return 1;
}

static void blit(HDC hdc, const uint32_t *pixels, int width, int height) {
    struct {
        BITMAPINFOHEADER header;
        RGBQUAD colors[3];
    } bi;
    if (!backbuffer_create(width, height)) {
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
    SetDIBitsToDevice(back_dc, 0, 0, width, height, 0, 0, 0, height, (void *)pixels, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    BitBlt(hdc, 0, 0, width, height, back_dc, 0, 0, SRCCOPY);
}

static void push_mouse_button(libpxl_event_type type, int button, int x, int y, int mods) {
    libpxl_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.button = button;
    event.x = x;
    event.y = y;
    event.mods = mods;
    push_event(&event);
}

static void push_key(libpxl_event_type type, int key, int mods, int repeat) {
    libpxl_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.key = key;
    event.mods = mods;
    event.repeat = repeat;
    push_event(&event);
}

LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                const uint32_t *pixels = libpxl_pixels();
                if (pixels) {
                    blit(hdc, pixels, libpxl_width(), libpxl_height());
                }
                EndPaint(hwnd, &ps);
            }
            return 0;
        case WM_ERASEBKGND:
            return 1; /* the framebuffer covers the client area */
        case WM_KEYDOWN:
        case WM_KEYUP: {
            int repeat = (msg == WM_KEYDOWN) ? (int)(((unsigned int)lParam >> 30) & 1) : 0;
            push_key((msg == WM_KEYDOWN) ? LIBPXL_KEYDOWN : LIBPXL_KEYUP,
                     translate_key(wParam, lParam), current_mods(), repeat);
        }
            return 0;
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP: {
            int repeat = (msg == WM_SYSKEYDOWN) ? (int)(((unsigned int)lParam >> 30) & 1) : 0;
            push_key((msg == WM_SYSKEYDOWN) ? LIBPXL_KEYDOWN : LIBPXL_KEYUP,
                     translate_key(wParam, lParam), current_mods(), repeat);
        }
            return DefWindowProc(hwnd, msg, wParam, lParam); /* keep Alt+F4 working */
        case WM_LBUTTONDOWN:
            push_mouse_button(LIBPXL_MOUSEDOWN, 1, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), current_mods());
            return 0;
        case WM_LBUTTONUP:
            push_mouse_button(LIBPXL_MOUSEUP, 1, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), current_mods());
            return 0;
        case WM_MBUTTONDOWN:
            push_mouse_button(LIBPXL_MOUSEDOWN, 2, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), current_mods());
            return 0;
        case WM_MBUTTONUP:
            push_mouse_button(LIBPXL_MOUSEUP, 2, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), current_mods());
            return 0;
        case WM_RBUTTONDOWN:
            push_mouse_button(LIBPXL_MOUSEDOWN, 3, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), current_mods());
            return 0;
        case WM_RBUTTONUP:
            push_mouse_button(LIBPXL_MOUSEUP, 3, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), current_mods());
            return 0;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
            push_mouse_button((msg == WM_XBUTTONDOWN) ? LIBPXL_MOUSEDOWN : LIBPXL_MOUSEUP,
                              (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? 4 : 5,
                              GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), current_mods());
            return TRUE;
        case WM_MOUSEMOVE:
            push_mouse_button(LIBPXL_MOUSEMOVE, 0, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), current_mods());
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
            push_event(&event);
        }
            return 0;
        case WM_SETFOCUS:
            push_key(LIBPXL_FOCUSGAINED, 0, current_mods(), 0);
            return 0;
        case WM_KILLFOCUS:
            push_key(LIBPXL_FOCUSLOST, 0, 0, 0);
            return 0;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            window = NULL;
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

int libpxl_backend_open(const char *title, int width, int height) {
    HINSTANCE instance = GetModuleHandle(NULL);
    WNDCLASSEX wc;
    RECT rect;
    MSG msg;
    LARGE_INTEGER freq;

    enable_dpi_awareness();

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_VREDRAW | CS_HREDRAW;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = instance;
    wc.lpszClassName = WINDOW_CLASS;
    if (RegisterClassEx(&wc)) {
        class_registered = 1;
    } else if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return 0;
    }

    /* drop anything left over from a previous run */
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
    }
    queue_head = 0;
    queue_tail = 0;

    if (!window_rect_for_client(width, height, &rect)) {
        libpxl_backend_close();
        return 0;
    }
    window = CreateWindowEx(WS_EX_CLIENTEDGE, WINDOW_CLASS, title,
                            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            rect.right - rect.left, rect.bottom - rect.top,
                            NULL, NULL, instance, NULL);
    if (!window) {
        libpxl_backend_close();
        return 0;
    }
    if (!QueryPerformanceFrequency(&freq)) {
        timer_freq = 0;
    } else {
        timer_freq = freq.QuadPart;
    }
    ShowWindow(window, SW_NORMAL);
    UpdateWindow(window);
    return 1;
}

void libpxl_backend_close(void) {
    backbuffer_destroy();
    if (window) {
        DestroyWindow(window);
        window = NULL;
    }
    if (class_registered) {
        UnregisterClass(WINDOW_CLASS, GetModuleHandle(NULL));
        class_registered = 0;
    }
    timer_freq = 0;
}

void libpxl_backend_present(const uint32_t *pixels, int width, int height) {
    HDC hdc = GetDC(window);
    if (hdc) {
        blit(hdc, pixels, width, height);
        ReleaseDC(window, hdc);
    }
}

int libpxl_backend_poll(libpxl_event *event) {
    MSG msg;
    for (;;) {
        if (pop_event(event)) {
            return 1;
        }
        if (!PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            return 0;
        }
        if (msg.message == WM_QUIT) {
            libpxl_event quit;
            memset(&quit, 0, sizeof(quit));
            quit.type = LIBPXL_QUIT;
            push_event(&quit);
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
