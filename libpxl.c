#include <stdlib.h>
#include <string.h>
#include "libpxl.h"
#include "libpxl_backend.h"

#define LIBPXL_KEY_STATE_MAX 0x200
#define LIBPXL_BUTTON_STATE_MAX 8

static uint32_t *pixels;
static int width;
static int height;
static int running;
static int fps = 60;
static int focused = 1;
static uint8_t key_state[LIBPXL_KEY_STATE_MAX];
static uint8_t button_state[LIBPXL_BUTTON_STATE_MAX];
static int mouse_x;
static int mouse_y;
static int mods;
static libpxl_draw_fn draw_fn;
static libpxl_event_fn event_fn;

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

void libpxl_set_fps(int fps_) {
    fps = fps_;
}

uint64_t libpxl_time(void) {
    return libpxl_backend_time();
}

void libpxl_sleep(int ms) {
    if (ms > 0) {
        libpxl_backend_delay(ms);
    }
}

int libpxl_key_down(int key) {
    if (key <= LIBPXL_KEY_UNKNOWN || key >= LIBPXL_KEY_STATE_MAX) {
        return 0;
    }
    return key_state[key] != 0;
}

int libpxl_mouse_down(int button) {
    if (button <= 0 || button >= LIBPXL_BUTTON_STATE_MAX) {
        return 0;
    }
    return button_state[button] != 0;
}

int libpxl_mouse_x(void) {
    return mouse_x;
}

int libpxl_mouse_y(void) {
    return mouse_y;
}

int libpxl_mods(void) {
    return mods;
}

int libpxl_focused(void) {
    return focused;
}

static void reset_input_state(void) {
    memset(key_state, 0, sizeof(key_state));
    memset(button_state, 0, sizeof(button_state));
    mouse_x = 0;
    mouse_y = 0;
    mods = 0;
    focused = 1;
}

static void update_state(const libpxl_event *event) {
    switch (event->type) {
        case LIBPXL_KEYDOWN:
        case LIBPXL_KEYUP:
            if (event->key > LIBPXL_KEY_UNKNOWN && event->key < LIBPXL_KEY_STATE_MAX) {
                key_state[event->key] = (uint8_t)(event->type == LIBPXL_KEYDOWN);
            }
            mods = event->mods;
            break;
        case LIBPXL_MOUSEDOWN:
        case LIBPXL_MOUSEUP:
            if (event->button > 0 && event->button < LIBPXL_BUTTON_STATE_MAX) {
                button_state[event->button] = (uint8_t)(event->type == LIBPXL_MOUSEDOWN);
            }
            mouse_x = event->x;
            mouse_y = event->y;
            mods = event->mods;
            break;
        case LIBPXL_MOUSEMOVE:
        case LIBPXL_MOUSEWHEEL:
            mouse_x = event->x;
            mouse_y = event->y;
            mods = event->mods;
            break;
        case LIBPXL_FOCUSLOST:
            focused = 0;
            mods = 0;
            break;
        case LIBPXL_FOCUSGAINED:
            focused = 1;
            mods = event->mods;
            break;
        default:
            break;
    }
}

static void dispatch_event(const libpxl_event *event) {
    if (event_fn) {
        event_fn(event);
    }
}

/* when the window loses focus we never see the matching releases, so emit them
   here to keep app state from sticking */
static void release_all_input(void) {
    libpxl_event event;
    int key;
    int button;

    for (key = LIBPXL_KEY_UNKNOWN + 1; key < LIBPXL_KEY_STATE_MAX; key++) {
        if (!key_state[key]) {
            continue;
        }
        memset(&event, 0, sizeof(event));
        event.type = LIBPXL_KEYUP;
        event.key = key;
        update_state(&event);
        dispatch_event(&event);
    }
    for (button = 1; button < LIBPXL_BUTTON_STATE_MAX; button++) {
        if (!button_state[button]) {
            continue;
        }
        memset(&event, 0, sizeof(event));
        event.type = LIBPXL_MOUSEUP;
        event.button = button;
        update_state(&event);
        dispatch_event(&event);
    }
}

int libpxl_run(const char *title, int width_, int height_, libpxl_draw_fn draw, libpxl_event_fn event) {
    uint64_t deadline;
    size_t bytes;

    if (!draw || width_ <= 0 || height_ <= 0) {
        return 1;
    }
    bytes = (size_t)width_ * (size_t)height_ * sizeof(*pixels);
    pixels = malloc(bytes);
    if (!pixels) {
        return 1;
    }
    memset(pixels, 0, bytes); /* no uninitialized first frame */
    width = width_;
    height = height_;
    draw_fn = draw;
    event_fn = event;
    reset_input_state();
    running = 1;
    if (!libpxl_backend_open(title, width, height)) {
        free(pixels);
        pixels = NULL;
        return 1;
    }
    deadline = libpxl_time();
    while (running) {
        libpxl_event event;
        while (libpxl_backend_poll(&event)) {
            if (event.type == LIBPXL_FOCUSLOST) {
                release_all_input();
            }
            update_state(&event);
            if (event.type == LIBPXL_QUIT) {
                running = 0;
            }
            if (event_fn) {
                event_fn(&event);
            }
        }
        if (!running) {
            break;
        }
        draw_fn();
        libpxl_backend_present(pixels, width, height);
        if (fps > 0) {
            uint64_t frame_ms = (uint64_t)(1000 / fps);
            uint64_t now;
            deadline += frame_ms;
            now = libpxl_time();
            if (deadline > now) {
                libpxl_sleep((int)(deadline - now));
            } else if (now - deadline > frame_ms) {
                deadline = now; /* fell behind: resync */
            }
        } else {
            deadline = libpxl_time();
        }
    }
    libpxl_backend_close();
    free(pixels);
    pixels = NULL;
    return 0;
}
