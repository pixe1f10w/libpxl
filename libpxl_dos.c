/* DOS backend: VESA VBE with a linear framebuffer, or mode 13h as a last
   resort. Build with DJGPP or OpenWatcom 2 for 32-bit DOS (protected mode):
   a 16-bit build cannot hold the framebuffer. Input comes from the BIOS
   keyboard and INT 33h mouse, timing from the BIOS tick and the PIT. */
#include <stdlib.h>
#include <string.h>
#include "libpxl.h"
#include "libpxl_backend.h"

#if defined(__DJGPP__)
#include <dos.h>
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <bios.h>
#include <sys/farptr.h>
#include <sys/movedata.h>
#include <sys/nearptr.h>
#define LIBPXL_DOS_DJGPP 1
#elif defined(__WATCOMC__)
#include <conio.h>
#include <bios.h>
#include <dos.h>
#include <i86.h>
#define LIBPXL_DOS_WATCOM 1
#else
#error "libpxl_dos.c needs DJGPP or OpenWatcom 2 for DOS"
#endif

#define LIBPXL_DOS_KEY_MAX 128
#define LIBPXL_DOS_VBE_MODES 0x220
#define LIBPXL_DOS_EVENT_QUEUE 64

/* --- port I/O ---------------------------------------------------------- */

#if defined(LIBPXL_DOS_DJGPP)
#define libpxl_inb(port) ((unsigned char)inportb((unsigned short)(port)))
#define libpxl_outb(port, value) outportb((unsigned short)(port), (unsigned char)(value))
#else
#define libpxl_inb(port) ((unsigned char)inp((unsigned short)(port)))
#define libpxl_outb(port, value) outp((unsigned short)(port), (unsigned char)(value))
#endif

/* --- interrupt calling -------------------------------------------------- */

struct libpxl_dos_regs {
    unsigned long eax, ebx, ecx, edx, esi, edi;
    unsigned short es, ds;
    unsigned short flags;
};

static int libpxl_dos_int(unsigned char vector, struct libpxl_dos_regs *regs) {
#if defined(LIBPXL_DOS_DJGPP)
    __dpmi_regs r;
    memset(&r, 0, sizeof(r));
    r.d.eax = regs->eax;
    r.d.ebx = regs->ebx;
    r.d.ecx = regs->ecx;
    r.d.edx = regs->edx;
    r.d.esi = regs->esi;
    r.d.edi = regs->edi;
    r.x.es = regs->es;
    r.x.ds = regs->ds;
    r.x.flags = regs->flags;
    if (__dpmi_int(vector, &r) == -1) {
        return 0;
    }
    regs->eax = r.d.eax;
    regs->ebx = r.d.ebx;
    regs->ecx = r.d.ecx;
    regs->edx = r.d.edx;
    regs->esi = r.d.esi;
    regs->edi = r.d.edi;
    regs->es = r.x.es;
    regs->ds = r.x.ds;
    regs->flags = r.x.flags;
    return 1;
#else
    union REGS r;
    struct SREGS s;
    memset(&r, 0, sizeof(r));
    segread(&s);
    r.x.eax = regs->eax;
    r.x.ebx = regs->ebx;
    r.x.ecx = regs->ecx;
    r.x.edx = regs->edx;
    r.x.esi = regs->esi;
    r.x.edi = regs->edi;
    s.es = regs->es;
    s.ds = regs->ds;
    int386x(vector, &r, &r, &s);
    regs->eax = r.x.eax;
    regs->ebx = r.x.ebx;
    regs->ecx = r.x.ecx;
    regs->edx = r.x.edx;
    regs->esi = r.x.esi;
    regs->edi = r.x.edi;
    regs->es = s.es;
    regs->ds = s.ds;
    regs->flags = r.x.cflag ? 1 : 0;
    return 1;
#endif
}

/* --- DOS memory blocks and the linear framebuffer ------------------------ */

struct libpxl_dos_block {
    unsigned short segment; /* usable as a real-mode segment (ES, linear = segment*16) */
    unsigned short handle;  /* accepted by the free call */
    unsigned short alt;    /* the other DPMI value, for the free fallback */
};

static int libpxl_dos_alloc(unsigned long bytes, struct libpxl_dos_block *block) {
    unsigned long paragraphs = (bytes + 15UL) / 16UL;
#if defined(LIBPXL_DOS_DJGPP)
    int selector = 0;
    int segment = __dpmi_allocate_dos_memory((int)paragraphs, &selector);
    if (segment == -1) {
        return 0;
    }
    block->segment = (unsigned short)segment;
    block->handle = (unsigned short)selector;
    block->alt = 0;
    return 1;
#else
    struct libpxl_dos_regs regs;
    memset(&regs, 0, sizeof(regs));
    regs.eax = 0x0100; /* DPMI 0.9: allocate a DOS memory block */
    regs.ebx = paragraphs;
    if (!libpxl_dos_int(0x31, &regs)) {
        return 0;
    }
    if (regs.flags & 0x0001) {
        return 0;
    }
    /* DPMI hosts differ over which return value is the segment: the value
       that the real-mode interrupt accepts in ES is the segment, and we check
       that when the controller info call runs */
    block->segment = (unsigned short)(regs.edx & 0xffff);
    block->handle = block->segment;
    block->alt = (unsigned short)(regs.eax & 0xffff);
    return 1;
#endif
}

static void libpxl_dos_free(struct libpxl_dos_block *block) {
#if defined(LIBPXL_DOS_DJGPP)
    __dpmi_free_dos_memory(block->handle);
#else
    struct libpxl_dos_regs regs;
    memset(&regs, 0, sizeof(regs));
    regs.eax = 0x0101; /* DPMI 0.9: free a DOS memory block */
    regs.edx = block->handle;
    libpxl_dos_int(0x31, &regs);
    if ((regs.flags & 0x0001) && block->alt && block->alt != block->handle) {
        memset(&regs, 0, sizeof(regs));
        regs.eax = 0x0101;
        regs.edx = block->alt;
        libpxl_dos_int(0x31, &regs);
    }
#endif
    block->segment = 0;
    block->handle = 0;
    block->alt = 0;
}

/* the DOS block is below 1MB, so it is addressable with a near pointer in
   the flat model, and through dosmemget()/dosmemput() elsewhere */
static void libpxl_dos_read(unsigned short segment, unsigned long offset, void *buffer, unsigned long bytes) {
#if defined(LIBPXL_DOS_DJGPP)
    dosmemget((unsigned long)segment * 16UL + offset, (size_t)bytes, buffer);
#else
    memcpy(buffer, (void *)((unsigned long)segment * 16UL + offset), (size_t)bytes);
#endif
}

static void libpxl_dos_write(unsigned short segment, unsigned long offset, const void *buffer, unsigned long bytes) {
#if defined(LIBPXL_DOS_DJGPP)
    dosmemput(buffer, (size_t)bytes, (unsigned long)segment * 16UL + offset);
#else
    memcpy((void *)((unsigned long)segment * 16UL + offset), buffer, (size_t)bytes);
#endif
}

/* --- packed VBE structures --------------------------------------------- */

#pragma pack(1)

struct libpxl_vbe_info {
    char signature[4];
    unsigned short version;
    unsigned long oem_string;
    unsigned long capabilities;
    unsigned long mode_list;
    unsigned short total_memory;
    unsigned char reserved[236];
};

struct libpxl_vbe_mode {
    unsigned short attributes;
    unsigned char win_a;
    unsigned char win_b;
    unsigned short granularity;
    unsigned short win_size;
    unsigned short win_a_segment;
    unsigned short win_b_segment;
    unsigned long win_func;
    unsigned short pitch;
    unsigned short width;
    unsigned short height;
    unsigned char char_w;
    unsigned char char_h;
    unsigned char planes;
    unsigned char bpp;
    unsigned char banks;
    unsigned char model;
    unsigned char bank_size;
    unsigned char image_pages;
    unsigned char reserved0;
    unsigned char red_mask;
    unsigned char red_pos;
    unsigned char green_mask;
    unsigned char green_pos;
    unsigned char blue_mask;
    unsigned char blue_pos;
    unsigned char rsvd_mask;
    unsigned char rsvd_pos;
    unsigned char direct_color;
    unsigned long phys_base;
    unsigned long offscreen;
    unsigned short offscreen_size;
    unsigned char reserved[206];
};

#pragma pack()

/* --- the backend ------------------------------------------------------- */

struct libpxl_backend {
    int width;
    int height;
    int bpp;               /* 8, 16, 24 or 32 */
    int mode;               /* VBE mode number, or 0x13 */
    unsigned long pitch;     /* bytes per scanline */
    unsigned char *lfb;      /* mapped framebuffer, NULL when selector-only */
    unsigned short lfb_selector; /* DJGPP fallback: LDT selector for the LFB */
    unsigned long lfb_handle;    /* DPMI mapping handle */
    unsigned long lfb_linear;    /* linear address of the mapping */
    unsigned long lfb_size;
    int nearptr;            /* DJGPP: near pointers were enabled */
    unsigned char *stage;    /* conversion buffer for 8/16/24bpp */
    unsigned long stage_bytes;
    struct libpxl_dos_block vbe; /* buffer for VBE info blocks */
    /* input */
    unsigned char key_down[LIBPXL_DOS_KEY_MAX]; /* by scan code */
    int have_mouse;
    int have_wheel;
    int mouse_buttons;
    int mouse_x;
    int mouse_y;
    libpxl_event queue[LIBPXL_DOS_EVENT_QUEUE];
    int queue_head;
    int queue_tail;
    /* timing */
    unsigned long base_ticks;
    unsigned long long base_ms;
    unsigned long long last_ms;
};

static libpxl_backend *libpxl_dos_current;
static const char *backend_error;

static void libpxl_dos_push(libpxl_backend *backend, const libpxl_event *event) {
    int next = (backend->queue_tail + 1) % LIBPXL_DOS_EVENT_QUEUE;
    if (next == backend->queue_head) {
        return; /* full: drop */
    }
    backend->queue[backend->queue_tail] = *event;
    backend->queue_tail = next;
}

/* --- VBE calls --------------------------------------------------------- */

static int libpxl_vbe_controller_info(libpxl_backend *backend, struct libpxl_vbe_info *info) {
    struct libpxl_dos_regs regs;
    unsigned short candidates[2];
    int i;
    /* DPMI hosts differ over which value is the real-mode segment, so try
       both and keep the one the BIOS actually wrote the info block to */
    candidates[0] = backend->vbe.segment;
    candidates[1] = backend->vbe.alt;
    for (i = 0; i < 2; i++) {
        if (!candidates[i] || (i == 1 && candidates[1] == candidates[0])) {
            continue;
        }
        libpxl_dos_write(candidates[i], 0, "VBE2", 4);
        memset(&regs, 0, sizeof(regs));
        regs.eax = 0x4F00;
        regs.es = candidates[i];
        regs.edi = 0;
        if (!libpxl_dos_int(0x10, &regs)) {
            continue;
        }
        if ((regs.eax & 0xffff) != 0x004F) {
            continue;
        }
        libpxl_dos_read(candidates[i], 0, info, sizeof(*info));
        if (memcmp(info->signature, "VESA", 4) != 0) {
            continue;
        }
        backend->vbe.segment = candidates[i];
        return 1;
    }
    return 0;
}

static int libpxl_vbe_mode_info(libpxl_backend *backend, unsigned short mode, struct libpxl_vbe_mode *info) {
    struct libpxl_dos_regs regs;
    memset(&regs, 0, sizeof(regs));
    regs.eax = 0x4F01;
    regs.ecx = mode;
    regs.es = backend->vbe.segment;
    regs.edi = 0;
    if (!libpxl_dos_int(0x10, &regs)) {
        return 0;
    }
    if ((regs.eax & 0xffff) != 0x004F) {
        return 0;
    }
    libpxl_dos_read(backend->vbe.segment, 0, info, sizeof(*info));
    return 1;
}

static int libpxl_vbe_set_mode(libpxl_backend *backend, unsigned short mode) {
    struct libpxl_dos_regs regs;
    (void)backend; /* the interrupt needs no window state */
    memset(&regs, 0, sizeof(regs));
    regs.eax = 0x4F02;
    regs.ebx = (unsigned long)mode | 0x4000UL; /* ask for the linear framebuffer */
    if (!libpxl_dos_int(0x10, &regs)) {
        return 0;
    }
    return (regs.eax & 0xffff) == 0x004F;
}

/* probe the mode number ranges instead of walking the real-mode mode list,
   which is a far pointer and awkward to read from a flat 32-bit program;
   4F01h rejects mode numbers the adapter does not have */
static unsigned short libpxl_vbe_pick_mode(libpxl_backend *backend, int *bpp) {
    static const int wanted[4] = {32, 24, 16, 8};
    unsigned short best_mode = 0;
    int best_bpp = 0;
    int best_rank = 4;
    unsigned short mode;
    for (mode = 0x100; mode < LIBPXL_DOS_VBE_MODES; mode++) {
        struct libpxl_vbe_mode info;
        int rank;
        if (!libpxl_vbe_mode_info(backend, mode, &info)) {
            continue;
        }
        if ((info.attributes & 0x1b) != 0x1b) { /* supported, color, graphics */
            continue;
        }
        if ((info.attributes & 0x80) == 0) { /* no linear framebuffer */
            continue;
        }
        if (info.planes != 1) {
            continue;
        }
        if ((int)info.width != backend->width || (int)info.height != backend->height) {
            continue;
        }
        if (info.phys_base == 0) {
            continue;
        }
        for (rank = 0; rank < 4; rank++) {
            if ((int)info.bpp == wanted[rank] && rank < best_rank) {
                best_mode = mode;
                best_bpp = wanted[rank];
                best_rank = rank;
            }
        }
    }
    *bpp = best_bpp;
    return best_mode;
}

/* --- the linear framebuffer --------------------------------------------- */

static int libpxl_lfb_map(libpxl_backend *backend, unsigned long physical, unsigned long bytes) {
#if defined(LIBPXL_DOS_DJGPP)
    __dpmi_meminfo mapping;
    int selector;
    mapping.handle = 0;
    mapping.address = physical;
    mapping.size = bytes;
    if (__dpmi_physical_address_mapping(&mapping) == -1) {
        return 0;
    }
    backend->lfb_linear = mapping.address;
    backend->lfb_handle = mapping.handle;
    backend->lfb_size = bytes;
    if (__djgpp_nearptr_enable() != 0) {
        if (mapping.address + bytes <= (unsigned long)__djgpp_selector_limit) {
            backend->lfb = (unsigned char *)(__djgpp_conventional_base + mapping.address);
            backend->nearptr = 1;
            return 1;
        }
        __djgpp_nearptr_disable();
    }
    /* fall back to an LDT descriptor and movedata() */
    selector = __dpmi_allocate_ldt_descriptors(1);
    if (selector < 0) {
        __dpmi_free_physical_address_mapping(&mapping);
        return 0;
    }
    if (__dpmi_set_segment_base_address(selector, mapping.address) == -1 ||
        __dpmi_set_segment_limit(selector, bytes - 1) == -1) {
        __dpmi_free_ldt_descriptor(selector);
        __dpmi_free_physical_address_mapping(&mapping);
        return 0;
    }
    backend->lfb_selector = (unsigned short)selector;
    backend->lfb = NULL;
    return 1;
#else
    struct libpxl_dos_regs regs;
    memset(&regs, 0, sizeof(regs));
    regs.eax = 0x0800; /* DPMI 0.9: map a physical address to linear memory */
    regs.ebx = physical >> 16;
    regs.ecx = physical & 0xffff;
    regs.esi = bytes >> 16;
    regs.edi = bytes & 0xffff;
    if (!libpxl_dos_int(0x31, &regs)) {
        return 0;
    }
    if (regs.flags & 0x0001) {
        return 0;
    }
    backend->lfb_linear = ((unsigned long)(regs.ebx & 0xffff) << 16) | (regs.ecx & 0xffff);
    backend->lfb_handle = ((unsigned long)(regs.esi & 0xffff) << 16) | (regs.edi & 0xffff);
    backend->lfb_size = bytes;
    /* the flat model addresses the whole linear space with near pointers */
    backend->lfb = (unsigned char *)backend->lfb_linear;
    return 1;
#endif
}

static void libpxl_lfb_unmap(libpxl_backend *backend) {
#if defined(LIBPXL_DOS_DJGPP)
    if (backend->nearptr) {
        __djgpp_nearptr_disable();
        backend->nearptr = 0;
    }
    if (backend->lfb_selector) {
        __dpmi_free_ldt_descriptor(backend->lfb_selector);
        backend->lfb_selector = 0;
    }
    if (backend->lfb_handle) {
        __dpmi_meminfo mapping;
        mapping.handle = backend->lfb_handle;
        mapping.address = backend->lfb_linear;
        mapping.size = backend->lfb_size;
        __dpmi_free_physical_address_mapping(&mapping);
        backend->lfb_handle = 0;
    }
#else
    if (backend->lfb_handle) {
        struct libpxl_dos_regs regs;
        memset(&regs, 0, sizeof(regs));
        regs.eax = 0x0801; /* DPMI 0.9: free a physical address mapping */
        regs.ebx = backend->lfb_handle >> 16;
        regs.ecx = backend->lfb_handle & 0xffff;
        libpxl_dos_int(0x31, &regs);
        backend->lfb_handle = 0;
    }
#endif
    backend->lfb = NULL;
    backend->lfb_linear = 0;
    backend->lfb_size = 0;
}

/* --- presentation ------------------------------------------------------ */

static void libpxl_present_line(libpxl_backend *backend, unsigned long offset, const void *source, unsigned long bytes) {
#if defined(LIBPXL_DOS_DJGPP)
    if (!backend->lfb) {
        movedata(_my_ds(), (unsigned)source, backend->lfb_selector, (unsigned)offset, (size_t)bytes);
        return;
    }
#endif
    memcpy(backend->lfb + offset, source, (size_t)bytes);
}

/* a 3-3-2 palette: red in bits 7..5, green in 4..2, blue in 1..0 */
static void libpxl_vga_palette(void) {
    int i;
    libpxl_outb(0x3C8, 0);
    for (i = 0; i < 256; i++) {
        libpxl_outb(0x3C9, (unsigned char)(((i >> 5) & 7) * 9));
        libpxl_outb(0x3C9, (unsigned char)(((i >> 2) & 7) * 9));
        libpxl_outb(0x3C9, (unsigned char)((i & 3) * 21));
    }
}

static void libpxl_dos_blit(libpxl_backend *backend, const uint32_t *pixels, int width, int height) {
    int y;
    unsigned long line_bytes;

    if (backend->bpp == 32 && backend->pitch == (unsigned long)width * 4UL) {
        libpxl_present_line(backend, 0, pixels,
                            (unsigned long)height * (unsigned long)width * 4UL);
        return;
    }
    for (y = 0; y < height; y++) {
        const uint32_t *source = pixels + (unsigned long)y * (unsigned long)width;
        unsigned char *target = backend->stage;
        int x;
        if (backend->bpp == 32) {
            line_bytes = (unsigned long)width * 4UL;
            libpxl_present_line(backend, (unsigned long)y * backend->pitch, source, line_bytes);
            continue;
        }
        if (backend->bpp == 24) {
            for (x = 0; x < width; x++) {
                uint32_t pixel = source[x];
                target[0] = (unsigned char)(pixel & 0xff);
                target[1] = (unsigned char)((pixel >> 8) & 0xff);
                target[2] = (unsigned char)((pixel >> 16) & 0xff);
                target += 3;
            }
            line_bytes = (unsigned long)width * 3UL;
        } else if (backend->bpp == 16) {
            for (x = 0; x < width; x++) {
                uint32_t pixel = source[x];
                unsigned int value = (unsigned int)(((pixel >> 19) & 0x1f) << 11) |
                                     (unsigned int)(((pixel >> 10) & 0x3f) << 5) |
                                     (unsigned int)((pixel >> 3) & 0x1f);
                target[x * 2] = (unsigned char)(value & 0xff);
                target[x * 2 + 1] = (unsigned char)(value >> 8);
            }
            line_bytes = (unsigned long)width * 2UL;
        } else {
            for (x = 0; x < width; x++) {
                uint32_t pixel = source[x];
                target[x] = (unsigned char)(((pixel >> 16) & 0xe0) |
                                           ((pixel >> 11) & 0x1c) |
                                           ((pixel >> 6) & 0x03));
            }
            line_bytes = (unsigned long)width;
        }
        libpxl_present_line(backend, (unsigned long)y * backend->pitch, target, line_bytes);
    }
}

/* --- input ------------------------------------------------------------- */

struct libpxl_dos_key_entry {
    unsigned char scan;
    unsigned char extended;
    int key;
};

static const struct libpxl_dos_key_entry libpxl_dos_keys[] = {
    { 0x01, 0, LIBPXL_KEY_ESCAPE },
    { 0x0E, 0, LIBPXL_KEY_BACKSPACE },
    { 0x0F, 0, LIBPXL_KEY_TAB },
    { 0x1C, 0, LIBPXL_KEY_ENTER },
    { 0x1D, 0, LIBPXL_KEY_LCTRL },
    { 0x2A, 0, LIBPXL_KEY_LSHIFT },
    { 0x36, 0, LIBPXL_KEY_RSHIFT },
    { 0x38, 0, LIBPXL_KEY_LALT },
    { 0x39, 0, LIBPXL_KEY_SPACE },
    { 0x3B, 0, LIBPXL_KEY_F1 },
    { 0x3C, 0, LIBPXL_KEY_F2 },
    { 0x3D, 0, LIBPXL_KEY_F3 },
    { 0x3E, 0, LIBPXL_KEY_F4 },
    { 0x3F, 0, LIBPXL_KEY_F5 },
    { 0x40, 0, LIBPXL_KEY_F6 },
    { 0x41, 0, LIBPXL_KEY_F7 },
    { 0x42, 0, LIBPXL_KEY_F8 },
    { 0x43, 0, LIBPXL_KEY_F9 },
    { 0x44, 0, LIBPXL_KEY_F10 },
    { 0x57, 0, LIBPXL_KEY_F11 },
    { 0x58, 0, LIBPXL_KEY_F12 },
    { 0x47, 0, '7' },
    { 0x48, 0, '8' },
    { 0x49, 0, '9' },
    { 0x4A, 0, '-' },
    { 0x4B, 0, '4' },
    { 0x4C, 0, '5' },
    { 0x4D, 0, '6' },
    { 0x4E, 0, '+' },
    { 0x4F, 0, '1' },
    { 0x50, 0, '2' },
    { 0x51, 0, '3' },
    { 0x52, 0, '0' },
    { 0x53, 0, '.' },
    { 0x37, 0, '*' },
    { 0x35, 0, '/' },
    { 0x47, 1, LIBPXL_KEY_HOME },
    { 0x48, 1, LIBPXL_KEY_UP },
    { 0x49, 1, LIBPXL_KEY_PAGEUP },
    { 0x4B, 1, LIBPXL_KEY_LEFT },
    { 0x4D, 1, LIBPXL_KEY_RIGHT },
    { 0x4F, 1, LIBPXL_KEY_END },
    { 0x50, 1, LIBPXL_KEY_DOWN },
    { 0x51, 1, LIBPXL_KEY_PAGEDOWN },
    { 0x52, 1, LIBPXL_KEY_INSERT },
    { 0x53, 1, LIBPXL_KEY_DELETE },
    { 0x1C, 1, LIBPXL_KEY_ENTER },
    { 0x1D, 1, LIBPXL_KEY_RCTRL },
    { 0x35, 1, '/' },
    { 0x38, 1, LIBPXL_KEY_RALT },
    { 0x5B, 1, LIBPXL_KEY_LSUPER },
    { 0x5C, 1, LIBPXL_KEY_RSUPER },
};

static int libpxl_dos_key(int scan, int extended) {
    size_t i;
    for (i = 0; i < sizeof(libpxl_dos_keys) / sizeof(libpxl_dos_keys[0]); i++) {
        if (libpxl_dos_keys[i].scan == (unsigned char)scan &&
            libpxl_dos_keys[i].extended == (unsigned char)extended) {
            return libpxl_dos_keys[i].key;
        }
    }
    return LIBPXL_KEY_UNKNOWN;
}

static int libpxl_dos_mods(void) {
    int flags = (int)_bios_keybrd(_NKEYBRD_SHIFTSTATUS) & 0xff;
    int mods = 0;
    if (flags & 0x03) {
        mods |= LIBPXL_MOD_SHIFT;
    }
    if (flags & 0x04) {
        mods |= LIBPXL_MOD_CTRL;
    }
    if (flags & 0x08) {
        mods |= LIBPXL_MOD_ALT;
    }
    return mods;
}

static void libpxl_push_key(libpxl_backend *backend, libpxl_event_type type, int key, int mods, int repeat) {
    libpxl_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.key = key;
    event.mods = mods;
    event.repeat = repeat;
    libpxl_dos_push(backend, &event);
}

static void libpxl_push_mouse(libpxl_backend *backend, libpxl_event_type type, int button, int x, int y, int wheel) {
    libpxl_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.button = button;
    event.x = x;
    event.y = y;
    event.wheel = wheel;
    event.mods = libpxl_dos_mods();
    libpxl_dos_push(backend, &event);
}

static void libpxl_poll_keys(libpxl_backend *backend) {
    int guard;
    for (guard = 0; guard < 64; guard++) {
        unsigned long code;
        unsigned char scan;
        unsigned char ascii;
        int extended;
        int key;
        int mods;
        int repeat;
        if (!_bios_keybrd(_NKEYBRD_READY)) {
            return;
        }
        code = (unsigned long)_bios_keybrd(_NKEYBRD_READ);
        scan = (unsigned char)((code >> 8) & 0xff);
        ascii = (unsigned char)(code & 0xff);
        extended = (ascii == 0 || ascii == 0xE0) ? 1 : 0;
        key = libpxl_dos_key(scan, extended);
        mods = libpxl_dos_mods();
        /* Alt+X is the documented way out on DOS */
        if (scan == 0x2D && (mods & LIBPXL_MOD_ALT)) {
            libpxl_event quit;
            memset(&quit, 0, sizeof(quit));
            quit.type = LIBPXL_QUIT;
            libpxl_dos_push(backend, &quit);
            continue;
        }
        repeat = (scan < LIBPXL_DOS_KEY_MAX && backend->key_down[scan]) ? 1 : 0;
        if (scan < LIBPXL_DOS_KEY_MAX) {
            backend->key_down[scan] = 1;
        }
        libpxl_push_key(backend, LIBPXL_KEYDOWN, key, mods, repeat);
    }
}

static int libpxl_mouse_init(libpxl_backend *backend) {
    struct libpxl_dos_regs regs;
    memset(&regs, 0, sizeof(regs));
    regs.eax = 0x0000; /* INT 33h: reset and detect */
    if (!libpxl_dos_int(0x33, &regs)) {
        return 0;
    }
    if ((regs.eax & 0xffff) != 0xFFFF) {
        return 0; /* no mouse driver */
    }
    memset(&regs, 0, sizeof(regs));
    regs.eax = 0x0001; /* show the cursor */
    libpxl_dos_int(0x33, &regs);
    memset(&regs, 0, sizeof(regs));
    regs.eax = 0x0011; /* CuteMouse: get the number of buttons */
    if (libpxl_dos_int(0x33, &regs) && (regs.eax & 0xffff) == 0x574D) {
        backend->have_wheel = (int)(regs.ecx & 1);
    }
    return 1;
}

static void libpxl_poll_mouse(libpxl_backend *backend) {
    struct libpxl_dos_regs regs;
    int buttons;
    int changed;
    int button;

    if (!backend->have_mouse) {
        return;
    }
    memset(&regs, 0, sizeof(regs));
    regs.eax = 0x0003; /* INT 33h: position and button state */
    if (!libpxl_dos_int(0x33, &regs)) {
        return;
    }
    buttons = (int)(regs.ebx & 0x07);
    backend->mouse_x = (int)(regs.ecx & 0xffff);
    backend->mouse_y = (int)(regs.edx & 0xffff);
    changed = buttons ^ backend->mouse_buttons;
    for (button = 0; button < 3; button++) {
        int mask = 1 << button;
        int number = (button == 0) ? 1 : (button == 1) ? 3 : 2; /* left, right, middle */
        if (changed & mask) {
            libpxl_push_mouse(backend,
                              (buttons & mask) ? LIBPXL_MOUSEDOWN : LIBPXL_MOUSEUP,
                              number, backend->mouse_x, backend->mouse_y, 0);
        }
    }
    backend->mouse_buttons = buttons;
    if (backend->have_wheel) {
        int wheel = (int)(signed char)((regs.ebx >> 8) & 0xff);
        if (wheel != 0) {
            libpxl_push_mouse(backend, LIBPXL_MOUSEWHEEL, 0,
                              backend->mouse_x, backend->mouse_y, -wheel);
        }
    }
}

/* --- timing ------------------------------------------------------------ */

static unsigned long libpxl_bios_ticks(void) {
#if defined(LIBPXL_DOS_DJGPP)
    return _farpeekl(_dos_ds, 0x46C);
#else
    /* the flat model maps the first megabyte at the same linear address */
    return *(volatile unsigned long *)0x46CUL;
#endif
}

static unsigned long libpxl_pit_read(void) {
    unsigned long low;
    unsigned long high;
    libpxl_outb(0x43, 0x00); /* latch counter 0 */
    low = libpxl_inb(0x40);
    high = libpxl_inb(0x40);
    return (high << 8) | low;
}

/* the BIOS tick counts 18.2 Hz, the PIT counter adds the sub-tick part */
static unsigned long long libpxl_dos_ms(libpxl_backend *backend) {
    unsigned long pit = libpxl_pit_read();
    unsigned long ticks = libpxl_bios_ticks();
    unsigned long long ms;
    if (ticks < backend->base_ticks) {
        /* midnight rollover: rebase to keep the clock monotonic */
        backend->base_ticks = ticks;
        backend->base_ms = backend->last_ms;
    }
    ms = backend->base_ms +
         (unsigned long long)(ticks - backend->base_ticks) * 54925ULL / 1000ULL +
         (unsigned long long)((65536UL - pit) * 1000UL) / 1193182UL;
    if (ms < backend->last_ms) {
        ms = backend->last_ms;
    }
    backend->last_ms = ms;
    return ms;
}

/* --- the backend contract ---------------------------------------------- */

libpxl_backend *libpxl_backend_open(const char *title, int width, int height) {
    libpxl_backend *backend;
    struct libpxl_vbe_info vbe;
    struct libpxl_vbe_mode info;
    unsigned short mode;
    int bpp = 0;
    unsigned long bytes;
    (void)title; /* DOS has no window title */

    backend_error = NULL;
    backend = (libpxl_backend *)malloc(sizeof(*backend));
    if (!backend) {
        backend_error = "libpxl: out of memory";
        return NULL;
    }
    memset(backend, 0, sizeof(*backend));
    backend->width = width;
    backend->height = height;

    if (!libpxl_dos_alloc(sizeof(vbe), &backend->vbe)) {
        backend_error = "libpxl: no DPMI memory for VBE";
        libpxl_backend_close(backend);
        return NULL;
    }
    if (libpxl_vbe_controller_info(backend, &vbe) && vbe.version >= 0x0102) {
        mode = libpxl_vbe_pick_mode(backend, &bpp);
        if (mode) {
            if (!libpxl_vbe_set_mode(backend, mode)) {
                backend_error = "libpxl: VESA mode could not be set";
                libpxl_backend_close(backend);
                return NULL;
            }
            if (!libpxl_vbe_mode_info(backend, mode, &info)) {
                backend_error = "libpxl: no VESA mode information";
                libpxl_backend_close(backend);
                return NULL;
            }
            backend->bpp = bpp;
            backend->pitch = info.pitch;
            backend->mode = mode;
            bytes = (unsigned long)info.pitch * (unsigned long)height;
            if (!libpxl_lfb_map(backend, info.phys_base, bytes)) {
                backend_error = "libpxl: the linear framebuffer cannot be mapped";
                libpxl_backend_close(backend);
                return NULL;
            }
        }
    }
    if (backend->bpp == 0) {
        struct libpxl_dos_regs regs;
        if (width != 320 || height != 200) {
            backend_error = "libpxl: no VESA mode with this size";
            libpxl_backend_close(backend);
            return NULL;
        }
        /* mode 13h: 320x200, 256 colors at 0xA0000 */
        memset(&regs, 0, sizeof(regs));
        regs.eax = 0x0013;
        if (!libpxl_dos_int(0x10, &regs)) {
            backend_error = "libpxl: mode 13h could not be set";
            libpxl_backend_close(backend);
            return NULL;
        }
#if defined(LIBPXL_DOS_DJGPP)
        if (__djgpp_nearptr_enable() == 0) {
            backend_error = "libpxl: near pointers are not available";
            libpxl_backend_close(backend);
            return NULL;
        }
        backend->nearptr = 1;
        backend->lfb = (unsigned char *)(__djgpp_conventional_base + 0xA0000);
#else
        backend->lfb = (unsigned char *)0xA0000UL;
#endif
        backend->bpp = 8;
        backend->pitch = 320;
        backend->mode = 0x13;
    }

    if (backend->bpp != 32) {
        backend->stage_bytes = (unsigned long)width * (unsigned long)((backend->bpp + 7) / 8);
        backend->stage = (unsigned char *)malloc(backend->stage_bytes);
        if (!backend->stage) {
            backend_error = "libpxl: out of memory";
            libpxl_backend_close(backend);
            return NULL;
        }
    }
    if (backend->bpp == 8) {
        libpxl_vga_palette();
    }

    backend->mouse_x = width / 2;
    backend->mouse_y = height / 2;
    backend->have_mouse = libpxl_mouse_init(backend);
    backend->base_ticks = libpxl_bios_ticks();
    backend->base_ms = 0;
    backend->last_ms = 0;
    libpxl_dos_current = backend;
    return backend;
}

void libpxl_backend_close(libpxl_backend *backend) {
    if (!backend) {
        return;
    }
    if (libpxl_dos_current == backend) {
        libpxl_dos_current = NULL;
    }
    if (backend->have_mouse) {
        struct libpxl_dos_regs regs;
        memset(&regs, 0, sizeof(regs));
        regs.eax = 0x0002; /* INT 33h: hide the cursor */
        libpxl_dos_int(0x33, &regs);
    }
    if (backend->bpp != 0) {
        struct libpxl_dos_regs regs;
        memset(&regs, 0, sizeof(regs));
        regs.eax = 0x0003; /* back to 80x25 text mode */
        libpxl_dos_int(0x10, &regs);
    }
    libpxl_lfb_unmap(backend);
    if (backend->vbe.segment) {
        libpxl_dos_free(&backend->vbe);
    }
    free(backend->stage);
    free(backend);
}

void libpxl_backend_present(libpxl_backend *backend, const uint32_t *pixels, int width, int height) {
    if (!backend || !pixels || (!backend->lfb && !backend->lfb_selector)) {
        return;
    }
    libpxl_dos_blit(backend, pixels, width, height);
}

int libpxl_backend_poll(libpxl_backend *backend, libpxl_event *event) {
    libpxl_poll_keys(backend);
    libpxl_poll_mouse(backend);
    if (backend->queue_head == backend->queue_tail) {
        return 0;
    }
    *event = backend->queue[backend->queue_head];
    backend->queue_head = (backend->queue_head + 1) % LIBPXL_DOS_EVENT_QUEUE;
    return 1;
}

void libpxl_backend_delay(int ms) {
    struct libpxl_dos_regs regs;
    unsigned long microseconds;
    int tries;

    if (ms <= 0) {
        return;
    }
    microseconds = (unsigned long)ms * 1000UL;
    memset(&regs, 0, sizeof(regs));
    regs.eax = 0x8600; /* INT 15h: wait */
    regs.ecx = microseconds >> 16;
    regs.edx = microseconds & 0xffff;
    for (tries = 0; tries < 4; tries++) {
        if (!libpxl_dos_int(0x15, &regs)) {
            break;
        }
        if ((regs.flags & 0x0001) == 0) {
            return; /* CF clear: the wait completed */
        }
    }
    /* fall back to a PIT busy-wait (1.193182 MHz) */
    {
        unsigned long target = (unsigned long)ms * 1193UL;
        unsigned long total = 0;
        unsigned long last = libpxl_pit_read();
        while (total < target) {
            unsigned long now = libpxl_pit_read();
            total += (last - now) & 0xffffUL;
            last = now;
        }
    }
}

uint64_t libpxl_backend_time(void) {
    if (!libpxl_dos_current) {
        return 0;
    }
    return (uint64_t)libpxl_dos_ms(libpxl_dos_current);
}

const char *libpxl_backend_error(void) {
    return backend_error;
}
