#include <windows.h>

LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        // handle events
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

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prevInstance, LPSTR pCmdLine,
                   int nCmdShow) {
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