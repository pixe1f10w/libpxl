#include <windows.h>
#include "libpxl.h"
#include "libpxl_backend.h"

static HWND window;

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

int libpxl_backend_poll(void) {
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return 0;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 1;
}

void libpxl_backend_delay(int ms) {
    Sleep((DWORD)ms);
}
