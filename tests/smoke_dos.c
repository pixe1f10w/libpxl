/* DOS smoke test: opens a VESA (or mode 13h) screen, draws three frames and
   writes result files the host can check. Build with DJGPP or OpenWatcom 2.
   Run it in DOSBox:  dosbox -c "mount c <dir>" -c "c:" -c "SMOKE.EXE" -c "exit" -exit */
#include <stdio.h>
#include "libpxl.h"

static int frames;
static int events;

static void mark(const char *name, int value) {
    FILE *f = fopen(name, "w");
    if (f) {
        fprintf(f, "%d\n", value);
        fclose(f);
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
}

static void input(const libpxl_event *event) {
    (void)event;
    events++;
}

int main(void) {
    int status;
    mark("SMOKE1.TXT", 1);
    libpxl_set_fps(0);
    status = libpxl_open("libpxl smoke", 320, 200);
    mark("SMOKE2.TXT", status);
    if (status == LIBPXL_OK) {
        libpxl_set_event_fn(input);
        while (libpxl_poll() > 0 && frames < 3) {
            draw();
            libpxl_present();
            frames++;
        }
        libpxl_close();
    }
    mark("SMOKE3.TXT", frames);
    printf("status=%d frames=%d events=%d\n", status, frames, events);
    return status;
}
