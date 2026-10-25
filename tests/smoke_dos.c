/* DOS smoke test: opens a VESA (or mode 13h) screen, draws a few frames and
   writes a result file the host can check. Build with DJGPP or OpenWatcom 2. */
#include <stdio.h>
#include "libpxl.h"

static FILE *log;
static int frames;
static int events;

static void note(const char *message) {
    if (log) {
        fprintf(log, "%s\n", message);
        fflush(log);
    }
}

static void draw(void) {
    int x;
    int y;
    for (y = 0; y < libpxl_height(); y++) {
        for (x = 0; x < libpxl_width(); x++) {
            LIBPXL_PIXEL(x, y) = 0x001f7f00;
        }
    }
    frames++;
}

static void input(const libpxl_event *event) {
    (void)event;
    events++;
}

int main(void) {
    int status;
    log = fopen("SMOKE.TXT", "w");
    note("start");
    libpxl_set_fps(0);
    status = libpxl_open("libpxl smoke", 320, 200);
    note(status == LIBPXL_OK ? "open=ok" : "open=failed");
    if (status == LIBPXL_OK) {
        note("opened");
        libpxl_set_event_fn(input);
        while (libpxl_poll() > 0 && frames < 3) {
            draw();
            libpxl_present();
        }
        note("presented");
        libpxl_close();
        note("closed");
    }
    if (log) {
        fprintf(log, "status=%d frames=%d events=%d\n", status, frames, events);
        fclose(log);
        log = NULL;
    }
    printf("status=%d frames=%d events=%d\n", status, frames, events);
    return status;
}
