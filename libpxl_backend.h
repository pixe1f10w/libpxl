#ifndef LIBPXL_BACKEND_H
#define LIBPXL_BACKEND_H

#include "libpxl.h"

/* implemented by each platform backend */
int libpxl_backend_open(const char *title, int width, int height);
void libpxl_backend_close(void);
void libpxl_backend_present(const uint32_t *pixels, int width, int height);
/* fill event, return 1 if one was available, 0 when the queue is empty */
int libpxl_backend_poll(libpxl_event *event);
/* sleep for the given number of milliseconds, to pace frames */
void libpxl_backend_delay(int ms);
/* monotonic milliseconds; the epoch is backend-specific */
uint64_t libpxl_backend_time(void);

#endif
