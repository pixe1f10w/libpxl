#include <stdio.h>
#include <string.h>
#include "libpxl.h"
#include "libpxl_null.h"

static int failures;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++; \
        } \
    } while (0)

static int frames;

static void draw_quit_after_three(void) {
    frames++;
    if (frames == 3) {
        libpxl_quit();
    }
}

static int event_count;
static libpxl_event last_event;

static void record_event(const libpxl_event *event) {
    event_count++;
    last_event = *event;
}

static void test_open_close(void) {
    CHECK(libpxl_open("test", 64, 48) == LIBPXL_OK);
    CHECK(libpxl_error() == NULL);
    CHECK(libpxl_width() == 64);
    CHECK(libpxl_height() == 48);
    CHECK(libpxl_pixels() != NULL);
    CHECK(libpxl_pixels()[0] == 0);
    CHECK(libpxl_open("test", 64, 48) == LIBPXL_ERR_ALREADY_OPEN);
    CHECK(libpxl_error() != NULL);
    CHECK(libpxl_open(NULL, 64, 48) == LIBPXL_ERR_ARGUMENT);
    CHECK(libpxl_open("test", 0, 48) == LIBPXL_ERR_ARGUMENT);
    libpxl_close();
    CHECK(libpxl_pixels() == NULL);
    libpxl_close(); /* safe when closed */
}

static void test_run_frames_and_quit(void) {
    frames = 0;
    CHECK(libpxl_run("test", 32, 24, draw_quit_after_three, NULL) == LIBPXL_OK);
    CHECK(frames == 3);
    CHECK(libpxl_null_presents() == 3);
    CHECK(libpxl_pixels() == NULL);
    CHECK(libpxl_run("test", 32, 24, NULL, NULL) == LIBPXL_ERR_ARGUMENT);
    CHECK(libpxl_error() != NULL);
}

static void test_split_loop(void) {
    CHECK(libpxl_open("test", 16, 16) == LIBPXL_OK);
    libpxl_quit();
    CHECK(libpxl_poll() == 0);
    libpxl_present(); /* harmless after quit was requested */
    libpxl_close();
    CHECK(libpxl_poll() == -1);
    CHECK(libpxl_error() != NULL);
    libpxl_present(); /* harmless when closed */
}

static void test_input_state(void) {
    libpxl_event event;
    CHECK(libpxl_open("test", 32, 24) == LIBPXL_OK);
    libpxl_set_event_fn(record_event);
    event_count = 0;

    memset(&event, 0, sizeof(event));
    event.type = LIBPXL_KEYDOWN;
    event.key = 'A';
    event.mods = LIBPXL_MOD_SHIFT;
    libpxl_null_push(&event);
    CHECK(libpxl_poll() == 1);
    CHECK(event_count == 1);
    CHECK(last_event.type == LIBPXL_KEYDOWN);
    CHECK(last_event.repeat == 0);
    CHECK(libpxl_key_down('A') == 1);
    CHECK(libpxl_mods() == LIBPXL_MOD_SHIFT);

    event.type = LIBPXL_KEYUP;
    event.key = 'A';
    libpxl_null_push(&event);
    CHECK(libpxl_poll() == 1);
    CHECK(libpxl_key_down('A') == 0);

    event.type = LIBPXL_MOUSEMOVE;
    event.key = 0;
    event.x = 10;
    event.y = 20;
    event.mods = 0;
    libpxl_null_push(&event);
    CHECK(libpxl_poll() == 1);
    CHECK(libpxl_mouse_x() == 10);
    CHECK(libpxl_mouse_y() == 20);

    event.type = LIBPXL_MOUSEDOWN;
    event.button = 1;
    libpxl_null_push(&event);
    CHECK(libpxl_poll() == 1);
    CHECK(libpxl_mouse_down(1) == 1);
    CHECK(libpxl_mouse_down(2) == 0);

    event.type = LIBPXL_MOUSEUP;
    event.button = 1;
    libpxl_null_push(&event);
    CHECK(libpxl_poll() == 1);
    CHECK(libpxl_mouse_down(1) == 0);

    event.type = LIBPXL_MOUSEWHEEL;
    event.wheel = 2;
    libpxl_null_push(&event);
    CHECK(libpxl_poll() == 1);
    CHECK(last_event.type == LIBPXL_MOUSEWHEEL);
    CHECK(last_event.wheel == 2);

    event.type = LIBPXL_RESIZE;
    event.x = 100;
    event.y = 50;
    libpxl_null_push(&event);
    CHECK(libpxl_poll() == 1);
    CHECK(last_event.type == LIBPXL_RESIZE);
    CHECK(last_event.x == 100);

    libpxl_close();
    CHECK(libpxl_poll() == -1);
    CHECK(libpxl_key_down('A') == 0);
    CHECK(libpxl_mouse_down(1) == 0);
}

static void test_focus(void) {
    libpxl_event event;
    CHECK(libpxl_open("test", 32, 24) == LIBPXL_OK);
    libpxl_set_event_fn(record_event);

    memset(&event, 0, sizeof(event));
    event.type = LIBPXL_KEYDOWN;
    event.key = LIBPXL_KEY_LEFT;
    event.mods = LIBPXL_MOD_CTRL;
    libpxl_null_push(&event);
    event.type = LIBPXL_MOUSEDOWN;
    event.button = 1;
    libpxl_null_push(&event);
    CHECK(libpxl_poll() == 1);
    CHECK(libpxl_poll() == 1);
    CHECK(libpxl_key_down(LIBPXL_KEY_LEFT) == 1);
    CHECK(libpxl_mouse_down(1) == 1);
    CHECK(libpxl_focused() == 1);

    event_count = 0;
    memset(&event, 0, sizeof(event));
    event.type = LIBPXL_FOCUSLOST;
    libpxl_null_push(&event);
    CHECK(libpxl_poll() == 1);
    /* releases for the held key and button, then the focus event */
    CHECK(event_count == 3);
    CHECK(libpxl_key_down(LIBPXL_KEY_LEFT) == 0);
    CHECK(libpxl_mouse_down(1) == 0);
    CHECK(libpxl_focused() == 0);
    CHECK(libpxl_mods() == 0);

    memset(&event, 0, sizeof(event));
    event.type = LIBPXL_FOCUSGAINED;
    libpxl_null_push(&event);
    CHECK(libpxl_poll() == 1);
    CHECK(libpxl_focused() == 1);

    libpxl_close();
}

static uint32_t app_buffer[32 * 24];
static int app_frames;

static void draw_app(void) {
    app_frames++;
    app_buffer[0] = 0x00123456;
    if (app_frames == 2) {
        libpxl_quit();
    }
}

static void test_app_pixels(void) {
    app_frames = 0;
    memset(app_buffer, 0, sizeof(app_buffer));
    libpxl_set_pixels(app_buffer);
    CHECK(libpxl_run("test", 32, 24, draw_app, NULL) == LIBPXL_OK);
    CHECK(app_frames == 2);
    CHECK(app_buffer[0] == 0x00123456);
    CHECK(libpxl_pixels() == NULL);

    libpxl_set_pixels(NULL);
    app_frames = 0;
    CHECK(libpxl_run("test", 32, 24, draw_app, NULL) == LIBPXL_OK);
    CHECK(app_frames == 2);
    CHECK(libpxl_pixels() == NULL);
}

static void test_time_and_sleep(void) {
    uint64_t before;
    uint64_t after;
    before = libpxl_time();
    libpxl_sleep(20);
    after = libpxl_time();
    CHECK(after - before == 20);
    CHECK(libpxl_error() == NULL);
}

int main(void) {
    test_open_close();
    test_run_frames_and_quit();
    test_split_loop();
    test_input_state();
    test_focus();
    test_app_pixels();
    test_time_and_sleep();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
