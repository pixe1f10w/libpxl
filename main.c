#include "libpxl.h"

#if defined(_WIN32)
#include <windows.h>
#endif

#define WIDTH 640
#define HEIGHT 480
#define MARKER 8

static unsigned frame = 0;
static int mouse_x = WIDTH / 2;
static int mouse_y = HEIGHT / 2;

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
    /* white square under the mouse */
    for (int y = mouse_y - MARKER; y <= mouse_y + MARKER; y++) {
        for (int x = mouse_x - MARKER; x <= mouse_x + MARKER; x++) {
            if ((unsigned)x < (unsigned)width && (unsigned)y < (unsigned)height) {
                pixels[y * width + x] = 0xffffff;
            }
        }
    }
    frame++;
}

static void input(const libpxl_event *event) {
    switch (event->type) {
        case LIBPXL_KEYDOWN:
            if (event->key == LIBPXL_KEY_ESCAPE) {
                libpxl_quit();
            }
            break;
        case LIBPXL_MOUSEMOVE:
            mouse_x = event->x;
            mouse_y = event->y;
            break;
        default:
            break;
    }
}

static int run(void) {
    return libpxl_run("libpxl demo", WIDTH, HEIGHT, draw, input);
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
