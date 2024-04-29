#include <stdio.h>
#include <stdint.h>

#if defined(_WIN32)
#include <windows.h>

#define WIDTH 640
#define HEIGHT 480
static const int width = WIDTH;
static const int height = HEIGHT;
static uint32_t pixels[WIDTH * HEIGHT];

LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        // handle events
        case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                HDC memdc = CreateCompatibleDC(hdc);
                HBITMAP hbmp = CreateCompatibleBitmap(hdc, width, height);
                HBITMAP oldbmp = SelectObject(memdc, hbmp);
                BITMAPINFO bi = {{sizeof(bi), width, -height, 1, 32, BI_BITFIELDS}};
                bi.bmiColors[0].rgbRed = bi.bmiColors[1].rgbGreen = bi.bmiColors[2].rgbBlue = 0xff;
                SetDIBitsToDevice(memdc, 0, 0, width, height, 0, 0, 0, height, pixels, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
                BitBlt(hdc, 0, 0, width, height, memdc, 0, 0, SRCCOPY);
                SelectObject(memdc, oldbmp);
                DeleteObject(hbmp);
                DeleteDC(memdc);
                EndPaint(hwnd, &ps);
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prevInstance, LPSTR pCmdLine, int nCmdShow) {
    MSG msg;
    HINSTANCE hInstance = GetModuleHandle(NULL);
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_VREDRAW | CS_HREDRAW;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "demo";
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindowEx(WS_EX_CLIENTEDGE, "demo", "libpxl demo",
                               WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                               640, 480, NULL, NULL, hInstance, NULL);
    ShowWindow(hwnd, SW_NORMAL);
    UpdateWindow(hwnd);
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_QUIT) {
            return 0;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}
#endif