#ifndef LIBPXL_H
#define LIBPXL_H

#include <stdint.h>

/* key codes: ASCII for printable keys, LIBPXL_KEY_* for the rest */
enum {
    LIBPXL_KEY_UNKNOWN = 0,
    LIBPXL_KEY_ESCAPE = 27,
    LIBPXL_KEY_SPACE = 32,
    LIBPXL_KEY_LEFT = 0x100,
    LIBPXL_KEY_RIGHT,
    LIBPXL_KEY_UP,
    LIBPXL_KEY_DOWN,
};

typedef enum {
    LIBPXL_QUIT, /* the user closed the window */
    LIBPXL_KEYDOWN,
    LIBPXL_KEYUP,
    LIBPXL_MOUSEDOWN,
    LIBPXL_MOUSEUP,
    LIBPXL_MOUSEMOVE,
} libpxl_event_type;

typedef struct {
    libpxl_event_type type;
    int key;      /* key events */
    int x, y;     /* mouse events, in pixels */
    int button;   /* mouse button events: 1 left, 2 middle, 3 right */
} libpxl_event;

/* called once per frame; the app fills the framebuffer */
typedef void (*libpxl_draw_fn)(void);
/* called for each input event; may be NULL */
typedef void (*libpxl_event_fn)(const libpxl_event *event);

/* open a window and run the frame loop until libpxl_quit() or the window closes */
int libpxl_run(const char *title, int width, int height, libpxl_draw_fn draw, libpxl_event_fn event);

/* ask the frame loop to stop */
void libpxl_quit(void);

/* the framebuffer: width * height pixels in 0x00RRGGBB */
uint32_t *libpxl_pixels(void);
int libpxl_width(void);
int libpxl_height(void);

#endif
