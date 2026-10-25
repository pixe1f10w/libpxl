#include "libpxl.h"

#if defined(_WIN32)
#include <windows.h>
#endif

#define WIDTH 640
#define HEIGHT 480
#define MARKER_MIN 2
#define MARKER_MAX 64

static unsigned frame = 0;
static int mouse_x = WIDTH / 2;
static int mouse_y = HEIGHT / 2;
static int marker = 8;

static void draw(void) {
    int width = libpxl_width();
    int height = libpxl_height();
    uint32_t marker_color = libpxl_mouse_down(1) ? 0xff0000 : 0xffffff;
    int x;
    int y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            uint8_t r = (uint8_t)(x + frame);
            uint8_t g = (uint8_t)(y + frame);
            uint8_t b = (uint8_t)((x + y + frame) >> 1);
            LIBPXL_PIXEL(x, y) = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
    /* square under the mouse; red while the left button is down */
    for (y = mouse_y - marker; y <= mouse_y + marker; y++) {
        for (x = mouse_x - marker; x <= mouse_x + marker; x++) {
            if ((unsigned)x < (unsigned)width && (unsigned)y < (unsigned)height) {
                LIBPXL_PIXEL(x, y) = marker_color;
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
        case LIBPXL_MOUSEWHEEL:
            marker += event->wheel * 2;
            if (marker < MARKER_MIN) {
                marker = MARKER_MIN;
            }
            if (marker > MARKER_MAX) {
                marker = MARKER_MAX;
            }
            break;
        default:
            break;
    }
}

static int run(void) {
    libpxl_set_fps(60);
    return libpxl_run("libpxl demo", WIDTH, HEIGHT, draw, input);
}

#if defined(_WIN32)
int WINAPI WinMain(HINSTANCE instance, HINSTANCE prevInstance, LPSTR pCmdLine, int nCmdShow) {
    (void)instance;
    (void)prevInstance;
    (void)pCmdLine;
    (void)nCmdShow;
    return run();
}
#else
int main(void) {
    return run();
}
#endif
