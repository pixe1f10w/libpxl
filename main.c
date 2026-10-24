#include "libpxl.h"

#if defined(_WIN32)
#include <windows.h>
#endif

#define WIDTH 640
#define HEIGHT 480

static unsigned frame = 0;

static void draw(void) {
    int width = libpxl_width();
    int height = libpxl_height();
    uint32_t *pixels = libpxl_pixels();
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t r = (uint8_t)(x + frame);
            uint8_t g = (uint8_t)(y + frame);
            uint8_t b = (uint8_t)((x + y + frame) >> 1);
            pixels[y * width + x] = (r << 16) | (g << 8) | b;
        }
    }
    frame++;
}

static int run(void) {
    return libpxl_run("libpxl demo", WIDTH, HEIGHT, draw);
}

#if defined(_WIN32)
int WINAPI WinMain(HINSTANCE instance, HINSTANCE prevInstance, LPSTR pCmdLine, int nCmdShow) {
    return run();
}
#else
int main(void) {
    return run();
}
#endif
