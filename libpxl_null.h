#ifndef LIBPXL_NULL_H
#define LIBPXL_NULL_H

#include "libpxl.h"

/* test hooks for the null backend, not part of the public API */
void libpxl_null_push(const libpxl_event *event);
unsigned long libpxl_null_presents(void);

#endif
