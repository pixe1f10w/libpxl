/* DOS input probe: reads the BIOS keyboard and the INT 33h mouse and writes
   what it sees to KEYLOG.TXT, so the platform behaviour can be checked against
   the backend's assumptions. Build it like tests/smoke_dos.c and run it with the
   input injection described in docs/verification.md. The executable name and the
   log name stay within 8.3. */
#include <stdio.h>
#include <string.h>
#include <bios.h>

#if defined(__DJGPP__)
#include <dpmi.h>
#elif defined(__WATCOMC__)
#include <i86.h>
#else
#error "keyprobe_dos.c needs DJGPP or OpenWatcom 2 for DOS"
#endif

struct probe_regs {
    unsigned long eax, ebx, ecx, edx, esi, edi;
    unsigned short es, ds;
    unsigned short flags;
};

/* kept in sync with libpxl_dos.c */
static int probe_int(unsigned char vector, struct probe_regs *regs) {
#if defined(__DJGPP__)
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
    if (__dpmi_int(vector, &r) == -1) {
        return 0;
    }
    regs->eax = r.d.eax;
    regs->ebx = r.d.ebx;
    regs->ecx = r.d.ecx;
    regs->edx = r.d.edx;
    regs->esi = r.d.esi;
    regs->edi = r.d.edi;
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

static FILE *log;

static void flush_log(void) {
    if (log) {
        fflush(log);
    }
}

int main(void) {
    unsigned long keys = 0;
    unsigned long iterations;
    int last_x = -1;
    int last_y = -1;
    int last_buttons = -1;
    int have_mouse;

    log = fopen("KEYLOG.TXT", "w");
    if (log) {
        fprintf(log, "start\n");
        flush_log();
    }

    /* INT 33h: reset and detect */
    {
        struct probe_regs regs;
        memset(&regs, 0, sizeof(regs));
        regs.eax = 0x0000;
        have_mouse = probe_int(0x33, &regs) && (regs.eax & 0xffff) == 0xFFFF;
        if (log) {
            fprintf(log, "mouse=%s buttons=%x\n",
                    have_mouse ? "yes" : "no", (unsigned)(regs.ebx & 0xffff));
            flush_log();
        }
    }

    for (iterations = 0; iterations < 3000000UL && keys < 3; iterations++) {
        if (_bios_keybrd(_NKEYBRD_READY)) {
            int classic = (int)_bios_keybrd(_KEYBRD_SHIFTSTATUS) & 0xff;
            int extended = (int)_bios_keybrd(_NKEYBRD_SHIFTSTATUS) & 0xff;
            unsigned long code = (unsigned long)_bios_keybrd(_NKEYBRD_READ);
            if (log) {
                fprintf(log, "key code=%lx scan=%x ascii=%x classic=%x extended=%x\n",
                        code, (unsigned)(code >> 8) & 0xff, (unsigned)code & 0xff,
                        classic, extended);
                flush_log();
            }
            keys++;
        }
        if (have_mouse) {
            struct probe_regs regs;
            int x;
            int y;
            int buttons;
            memset(&regs, 0, sizeof(regs));
            regs.eax = 0x0003; /* INT 33h: position and button state */
            if (probe_int(0x33, &regs)) {
                x = (int)(regs.ecx & 0xffff);
                y = (int)(regs.edx & 0xffff);
                buttons = (int)(regs.ebx & 0x07);
                if (x != last_x || y != last_y || buttons != last_buttons) {
                    if (log) {
                        fprintf(log, "mouse x=%d y=%d buttons=%x\n", x, y, buttons);
                        flush_log();
                    }
                    last_x = x;
                    last_y = y;
                    last_buttons = buttons;
                }
            }
        }
    }
    if (log) {
        fprintf(log, "done keys=%lu\n", keys);
        fclose(log);
        log = NULL;
    }
    return 0;
}
