#include <stdlib.h>
#include <string.h>
#include "libpxl.h"
#include "libpxl_backend.h"
#include "libpxl_null.h"

#define NULL_EVENT_QUEUE 64

struct libpxl_backend {
    int width;
    int height;
};

static libpxl_event queue[NULL_EVENT_QUEUE];
static int queue_head;
static int queue_tail;
static unsigned long presents;
static uint64_t clock_ms;
static const char *null_error;

libpxl_backend *libpxl_backend_open(const char *title, int width, int height) {
    libpxl_backend *backend;
    (void)title;
    backend = (libpxl_backend *)malloc(sizeof(*backend));
    if (!backend) {
        null_error = "libpxl: out of memory";
        return NULL;
    }
    backend->width = width;
    backend->height = height;
    queue_head = 0;
    queue_tail = 0;
    presents = 0;
    clock_ms = 0;
    null_error = NULL;
    return backend;
}

void libpxl_backend_close(libpxl_backend *backend) {
    free(backend);
}

void libpxl_backend_present(libpxl_backend *backend, const uint32_t *pixels, int width, int height) {
    (void)backend;
    (void)pixels;
    (void)width;
    (void)height;
    presents++;
}

int libpxl_backend_poll(libpxl_backend *backend, libpxl_event *event) {
    (void)backend;
    if (queue_head == queue_tail) {
        return 0;
    }
    *event = queue[queue_head];
    queue_head = (queue_head + 1) % NULL_EVENT_QUEUE;
    return 1;
}

void libpxl_backend_delay(int ms) {
    clock_ms += (uint64_t)ms; /* the fake clock advances, so tests stay fast */
}

uint64_t libpxl_backend_time(void) {
    return clock_ms;
}

const char *libpxl_backend_error(void) {
    return null_error;
}

void libpxl_null_push(const libpxl_event *event) {
    int next = (queue_tail + 1) % NULL_EVENT_QUEUE;
    if (next == queue_head) {
        return;
    }
    queue[queue_tail] = *event;
    queue_tail = next;
}

unsigned long libpxl_null_presents(void) {
    return presents;
}
