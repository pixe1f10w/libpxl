#include <windows.h>
#include <windowsx.h>
#include "libpxl.h"
#include "libpxl_backend.h"

#define EVENT_QUEUE 32

static HWND window;
static libpxl_event queue[EVENT_QUEUE];
static int queue_head;
static int queue_tail;

static void push_event(const libpxl_event *event) {
    int next = (queue_tail + 1) % EVENT_QUEUE;
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

static int translate_key(WPARAM key) {
    switch (key) {
        case VK_ESCAPE: return LIBPXL_KEY_ESCAPE;
        case VK_LEFT: return LIBPXL_KEY_LEFT;
        case VK_RIGHT: return LIBPXL_KEY_RIGHT;
        case VK_UP: return LIBPXL_KEY_UP;
        case VK_DOWN: return LIBPXL_KEY_DOWN;
        default:
            if (key >= '0' && key <= '9') return (int)key;
            if (key >= 'A' && key <= 'Z') return (int)key;
            return LIBPXL_KEY_UNKNOWN;
    }
}

static void blit(HDC hdc, const uint32_t *pixels, int width, int height) {
    HDC memdc = CreateCompatibleDC(hdc);
    HBITMAP hbmp = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldbmp = SelectObject(memdc, hbmp);
    struct {
        BITMAPINFOHEADER header;
        RGBQUAD colors[3];
    } bi = {{sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_BITFIELDS}};
    bi.colors[0].rgbRed = bi.colors[1].rgbGreen = bi.colors[2].rgbBlue = 0xff;
    SetDIBitsToDevice(memdc, 0, 0, width, height, 0, 0, 0, height, (void *)pixels, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    BitBlt(hdc, 0, 0, width, height, memdc, 0, 0, SRCCOPY);
    SelectObject(memdc, oldbmp);
    DeleteObject(hbmp);
    DeleteDC(memdc);
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
        case WM_KEYDOWN:
        case WM_KEYUP: {
            libpxl_event event = {0};
            event.type = (msg == WM_KEYDOWN) ? LIBPXL_KEYDOWN : LIBPXL_KEYUP;
            event.key = translate_key(wParam);
            push_event(&event);
        }
            return 0;
        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP: {
            libpxl_event event = {0};
            event.type = (msg == WM_LBUTTONDOWN || msg == WM_MBUTTONDOWN || msg == WM_RBUTTONDOWN)
                        ? LIBPXL_MOUSEDOWN : LIBPXL_MOUSEUP;
            event.x = GET_X_LPARAM(lParam);
            event.y = GET_Y_LPARAM(lParam);
            event.button = (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) ? 1
                         : (msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP) ? 2 : 3;
            push_event(&event);
        }
            return 0;
        case WM_MOUSEMOVE: {
            libpxl_event event = {0};
            event.type = LIBPXL_MOUSEMOVE;
            event.x = GET_X_LPARAM(lParam);
            event.y = GET_Y_LPARAM(lParam);
            push_event(&event);
        }
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

int libpxl_backend_open(const char *title, int width, int height) {
    HINSTANCE instance = GetModuleHandle(NULL);
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_VREDRAW | CS_HREDRAW;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = instance;
    wc.lpszClassName = "libpxl";
    if (!RegisterClassEx(&wc)) {
        return 0;
    }
    window = CreateWindowEx(WS_EX_CLIENTEDGE, "libpxl", title,
                            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            width, height, NULL, NULL, instance, NULL);
    if (!window) {
        return 0;
    }
    ShowWindow(window, SW_NORMAL);
    UpdateWindow(window);
    return 1;
}

void libpxl_backend_close(void) {
    if (window) {
        DestroyWindow(window);
        window = NULL;
    }
}

void libpxl_backend_present(const uint32_t *pixels, int width, int height) {
    HDC hdc = GetDC(window);
    blit(hdc, pixels, width, height);
    ReleaseDC(window, hdc);
}

int libpxl_backend_poll(libpxl_event *event) {
    for (;;) {
        if (pop_event(event)) {
            return 1;
        }
        MSG msg;
        if (!PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            return 0;
        }
        if (msg.message == WM_QUIT) {
            libpxl_event quit = {0};
            quit.type = LIBPXL_QUIT;
            push_event(&quit);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void libpxl_backend_delay(int ms) {
    Sleep((DWORD)ms);
}
