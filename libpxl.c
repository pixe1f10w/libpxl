#include <stdlib.h>
#include <string.h>
#include "libpxl.h"
#include "libpxl_backend.h"

#define LIBPXL_KEY_STATE_MAX 0x200
#define LIBPXL_BUTTON_STATE_MAX 8

static libpxl_backend *backend;
static uint32_t *pixels;
static uint32_t *owned_pixels;
static uint32_t *app_pixels;
static int width;
static int height;
static int opened;
static int quit_requested;
static int fps = 60;
static int focused = 1;
static uint8_t key_state[LIBPXL_KEY_STATE_MAX];
static uint8_t button_state[LIBPXL_BUTTON_STATE_MAX];
static int mouse_x;
static int mouse_y;
static int mods;
static libpxl_draw_fn draw_fn;
static libpxl_event_fn event_fn;
static const char *error_message;

static void set_error(const char *message) {
    error_message = message;
}

const char *libpxl_error(void) {
    return error_message;
}

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
    quit_requested = 1;
}

void libpxl_set_fps(int fps_) {
    fps = fps_;
}

void libpxl_set_event_fn(libpxl_event_fn event) {
    event_fn = event;
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

void libpxl_set_pixels(uint32_t *buffer) {
    size_t bytes;

    app_pixels = buffer;
    if (!opened) {
        pixels = buffer;
        return;
    }
    if (buffer) {
        pixels = buffer;
        return;
    }
    if (!owned_pixels) {
        bytes = (size_t)width * (size_t)height * sizeof(*pixels);
        owned_pixels = malloc(bytes);
        if (!owned_pixels) {
            set_error("libpxl_set_pixels: out of memory");
            return;
        }
        memset(owned_pixels, 0, bytes);
    }
    pixels = owned_pixels;
}

int libpxl_open(const char *title, int width_, int height_) {
    size_t bytes;

    if (!title || width_ <= 0 || height_ <= 0) {
        set_error("libpxl_open: invalid arguments");
        return LIBPXL_ERR_ARGUMENT;
    }
    if (opened) {
        set_error("libpxl_open: a window is already open");
        return LIBPXL_ERR_ALREADY_OPEN;
    }
    set_error(NULL);
    if (app_pixels) {
        pixels = app_pixels;
    } else {
        bytes = (size_t)width_ * (size_t)height_ * sizeof(*pixels);
        owned_pixels = malloc(bytes);
        if (!owned_pixels) {
            set_error("libpxl_open: out of memory");
            return LIBPXL_ERR_MEMORY;
        }
        memset(owned_pixels, 0, bytes); /* no uninitialized first frame */
        pixels = owned_pixels;
    }
    width = width_;
    height = height_;
    reset_input_state();
    quit_requested = 0;
    backend = libpxl_backend_open(title, width, height);
    if (!backend) {
        free(owned_pixels);
        owned_pixels = NULL;
        pixels = NULL;
        width = 0;
        height = 0;
        set_error(libpxl_backend_error());
        if (!error_message) {
            set_error("libpxl_open: the platform backend failed");
        }
        return LIBPXL_ERR_BACKEND;
    }
    opened = 1;
    return LIBPXL_OK;
}

int libpxl_poll(void) {
    libpxl_event event;

    if (!opened) {
        set_error("libpxl_poll: no window is open");
        return -1;
    }
    while (libpxl_backend_poll(backend, &event)) {
        if (event.type == LIBPXL_FOCUSLOST) {
            release_all_input();
        }
        update_state(&event);
        if (event.type == LIBPXL_QUIT) {
            quit_requested = 1;
        }
        if (event_fn) {
            event_fn(&event);
        }
    }
    return quit_requested ? 0 : 1;
}

void libpxl_present(void) {
    if (!opened) {
        set_error("libpxl_present: no window is open");
        return;
    }
    libpxl_backend_present(backend, pixels, width, height);
}

void libpxl_close(void) {
    if (!opened) {
        return;
    }
    libpxl_backend_close(backend);
    backend = NULL;
    opened = 0;
    free(owned_pixels);
    owned_pixels = NULL;
    pixels = NULL;
    width = 0;
    height = 0;
    quit_requested = 0;
    reset_input_state();
}

int libpxl_run(const char *title, int width, int height, libpxl_draw_fn draw, libpxl_event_fn event) {
    int status;
    uint64_t deadline;

    if (!draw) {
        set_error("libpxl_run: the draw callback is required");
        return LIBPXL_ERR_ARGUMENT;
    }
    status = libpxl_open(title, width, height);
    if (status != LIBPXL_OK) {
        return status;
    }
    draw_fn = draw;
    event_fn = event;
    deadline = libpxl_time();
    while ((status = libpxl_poll()) > 0) {
        draw_fn();
        libpxl_present();
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
    libpxl_close();
    return (status < 0) ? LIBPXL_ERR_NOT_OPEN : LIBPXL_OK;
}
