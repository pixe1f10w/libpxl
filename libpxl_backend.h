#ifndef LIBPXL_BACKEND_H
#define LIBPXL_BACKEND_H

#include "libpxl.h"

/* implemented by each platform backend */
int libpxl_backend_open(const char *title, int width, int height);
void libpxl_backend_close(void);
void libpxl_backend_present(const uint32_t *pixels, int width, int height);
/* return 0 once the window has been closed */
int libpxl_backend_poll(void);
/* sleep for the given number of milliseconds, to pace frames */
void libpxl_backend_delay(int ms);

#endif
