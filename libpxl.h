#ifndef LIBPXL_H
#define LIBPXL_H

#include <stdint.h>

/* called once per frame; the app fills the framebuffer */
typedef void (*libpxl_draw_fn)(void);

/* open a window and run the frame loop until libpxl_quit() or the window closes */
int libpxl_run(const char *title, int width, int height, libpxl_draw_fn draw);

/* ask the frame loop to stop */
void libpxl_quit(void);

/* the framebuffer: width * height pixels in 0x00RRGGBB */
uint32_t *libpxl_pixels(void);
int libpxl_width(void);
int libpxl_height(void);

#endif
