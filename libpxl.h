#ifndef LIBPXL_H
#define LIBPXL_H

#include <stddef.h>
#include <stdint.h>

/* key codes: printable keys use their ASCII value, the control keys use the
   standard ASCII control codes (backspace 8, tab 9, enter 13, escape 27), and
   the remaining keys are LIBPXL_KEY_* values >= 0x100 */
enum {
    LIBPXL_KEY_UNKNOWN = 0,
    LIBPXL_KEY_BACKSPACE = 8,
    LIBPXL_KEY_TAB = 9,
    LIBPXL_KEY_ENTER = 13,
    LIBPXL_KEY_ESCAPE = 27,
    LIBPXL_KEY_SPACE = 32,
    LIBPXL_KEY_LEFT = 0x100,
    LIBPXL_KEY_RIGHT,
    LIBPXL_KEY_UP,
    LIBPXL_KEY_DOWN,
    LIBPXL_KEY_DELETE,
    LIBPXL_KEY_INSERT,
    LIBPXL_KEY_HOME,
    LIBPXL_KEY_END,
    LIBPXL_KEY_PAGEUP,
    LIBPXL_KEY_PAGEDOWN,
    LIBPXL_KEY_F1,
    LIBPXL_KEY_F2,
    LIBPXL_KEY_F3,
    LIBPXL_KEY_F4,
    LIBPXL_KEY_F5,
    LIBPXL_KEY_F6,
    LIBPXL_KEY_F7,
    LIBPXL_KEY_F8,
    LIBPXL_KEY_F9,
    LIBPXL_KEY_F10,
    LIBPXL_KEY_F11,
    LIBPXL_KEY_F12,
    LIBPXL_KEY_LSHIFT,
    LIBPXL_KEY_RSHIFT,
    LIBPXL_KEY_LCTRL,
    LIBPXL_KEY_RCTRL,
    LIBPXL_KEY_LALT,
    LIBPXL_KEY_RALT,
    LIBPXL_KEY_LSUPER,
    LIBPXL_KEY_RSUPER
};

/* modifier bits carried by every event */
enum {
    LIBPXL_MOD_SHIFT = 1,
    LIBPXL_MOD_CTRL = 2,
    LIBPXL_MOD_ALT = 4,
    LIBPXL_MOD_SUPER = 8
};

/* error codes returned by libpxl_open(), libpxl_run() and libpxl_poll() */
enum {
    LIBPXL_OK = 0,
    LIBPXL_ERR_ARGUMENT = 1,     /* invalid argument */
    LIBPXL_ERR_ALREADY_OPEN = 2, /* a window is already open */
    LIBPXL_ERR_NOT_OPEN = 3,     /* no window is open */
    LIBPXL_ERR_MEMORY = 4,       /* out of memory */
    LIBPXL_ERR_BACKEND = 5        /* the platform backend failed */
};

typedef enum {
    LIBPXL_QUIT, /* the user closed the window */
    LIBPXL_KEYDOWN,
    LIBPXL_KEYUP,
    LIBPXL_MOUSEDOWN,
    LIBPXL_MOUSEUP,
    LIBPXL_MOUSEMOVE,
    LIBPXL_MOUSEWHEEL, /* wheel: notches, positive = away from the user */
    LIBPXL_RESIZE,     /* x, y = new client size; not emitted while minimized */
    LIBPXL_FOCUSLOST,  /* KEYUP/MOUSEUP are synthesized for held input */
    LIBPXL_FOCUSGAINED
} libpxl_event_type;

typedef struct {
    libpxl_event_type type;
    int key;      /* key events */
    int x, y;     /* mouse and resize events, in pixels */
    int button;   /* mouse button events: 1 left, 2 middle, 3 right, 4/5 extra */
    int wheel;    /* LIBPXL_MOUSEWHEEL: notches, positive = away from the user */
    int mods;     /* LIBPXL_MOD_* held when the event was produced */
    int repeat;   /* LIBPXL_KEYDOWN: 1 when the key is repeating */
} libpxl_event;

/* called once per frame; the app fills the framebuffer */
typedef void (*libpxl_draw_fn)(void);
/* called for each input event; may be NULL */
typedef void (*libpxl_event_fn)(const libpxl_event *event);

/* open a window and run the frame loop until libpxl_quit() or the window closes */
int libpxl_run(const char *title, int width, int height, libpxl_draw_fn draw, libpxl_event_fn event);

/* the lifecycle split into pieces, for apps that own their frame loop:
   libpxl_open() creates the window, libpxl_poll() pumps events (returns
   1 to continue, 0 when quit was requested, -1 on error), libpxl_present()
   shows the framebuffer and libpxl_close() releases everything */
int libpxl_open(const char *title, int width, int height);
int libpxl_poll(void);
void libpxl_present(void);
void libpxl_close(void);

/* register the event callback used by libpxl_poll(); may be NULL */
void libpxl_set_event_fn(libpxl_event_fn event);

/* ask the frame loop to stop */
void libpxl_quit(void);

/* target frame rate for libpxl_run(): 0 runs uncapped (default 60) */
void libpxl_set_fps(int fps);

/* monotonic milliseconds; the epoch is unspecified */
uint64_t libpxl_time(void);

/* sleep for the given number of milliseconds */
void libpxl_sleep(int ms);

/* the framebuffer: width * height pixels in 0x00RRGGBB, NULL while closed */
uint32_t *libpxl_pixels(void);
int libpxl_width(void);
int libpxl_height(void);

/* index the framebuffer; safe when int is only 16 bits wide */
#define LIBPXL_PIXEL(x, y) (libpxl_pixels()[(size_t)(y) * (size_t)libpxl_width() + (size_t)(x)])

/* use an app-provided framebuffer of width * height pixels; NULL restores the
   library-owned one; may be called before libpxl_open() or between frames */
void libpxl_set_pixels(uint32_t *pixels);

/* input state, updated before every event callback */
int libpxl_key_down(int key);
int libpxl_mouse_down(int button);
int libpxl_mouse_x(void);
int libpxl_mouse_y(void);
int libpxl_mods(void);
int libpxl_focused(void);

/* the last error message, NULL when there is none */
const char *libpxl_error(void);

#endif
