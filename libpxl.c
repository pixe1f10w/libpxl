#include <stdlib.h>
#include "libpxl.h"
#include "libpxl_backend.h"

static uint32_t *pixels;
static int width;
static int height;
static int running;
static libpxl_draw_fn draw_fn;

uint32_t *libpxl_pixels(void) {
    return pixels;
}

int libpxl_width(void) {
    return width;
}

int libpxl_height(void) {
    return height;
}

void libpxl_quit(void) {
    running = 0;
}

int libpxl_run(const char *title, int width_, int height_, libpxl_draw_fn draw) {
    if (!draw || width_ <= 0 || height_ <= 0) {
        return 1;
    }
    pixels = malloc((size_t)width_ * (size_t)height_ * sizeof(*pixels));
    if (!pixels) {
        return 1;
    }
    width = width_;
    height = height_;
    draw_fn = draw;
    running = 1;
    if (!libpxl_backend_open(title, width, height)) {
        free(pixels);
        pixels = NULL;
        return 1;
    }
    while (running) {
        if (!libpxl_backend_poll()) {
            break;
        }
        draw_fn();
        libpxl_backend_present(pixels, width, height);
        libpxl_backend_delay(16); /* ~60 fps */
    }
    libpxl_backend_close();
    free(pixels);
    pixels = NULL;
    return 0;
}
