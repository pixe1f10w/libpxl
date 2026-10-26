# libpxl architecture review & remodel proposals

Status: proposal — no code changes made yet.
Date: 2026-10-24

Scope: full review of `libpxl.h`, `libpxl.c`, `libpxl_backend.h`, `libpxl_win32.c`,
`main.c`, `CMakeLists.txt`, compared against a library of similar intent
(small, dependency-free, cross-platform framebuffer + window library).

---

## 1. Side-by-side architecture

| Aspect | libpxl today | Reference library | Verdict |
|---|---|---|---|
| Packaging | 5 files, static lib + CMake, explicit backend interface | 1 header, no build system, backend via preprocessor | **Keep ours** (see P3 for optional amalgamation) |
| State | File-scope globals in `libpxl.c` + statics in backend | One caller-owned struct; backend state fetched via `GWLP_USERDATA` | **Adapt** (backend handle, P2) |
| Lifecycle | One blocking `libpxl_run()` does open→loop→close | `open` / `loop` / `close`; app owns the loop | **Adopt the split** (P2) |
| Main loop | Library-owned loop, internal `delay(16)` | App-owned loop, library exposes `time`/`sleep` | **Adopt time/sleep; keep `run` as wrapper** |
| Input | Callback per event, backend ring buffer | Polling state only (`keys[256]`, `x`, `y`, `mouse`, `mod`) | **Hybrid: keep callbacks, add state queries + mods** (P1) |
| Key codes | ASCII + `0x100+` for arrows; mapping covers ~4 keys | 256-slot table; arrows = 17..20 (collides with Ctrl codes) | **Keep our encoding, extend + table-drive** (P1) |
| Framebuffer | Library-owned, `malloc` in `run`, exposed via `libpxl_pixels()` | Caller-owned/assignable pointer (lets apps point it at an existing buffer) | **Keep library-owned; optionally allow app buffer** (P3) |
| Presentation | Per-frame `CreateCompatibleDC`+`CreateCompatibleBitmap`+`SetDIBitsToDevice`+`BitBlt` | Same per-frame GDI churn on Win32; zero-copy `XImage`/`CGImage` elsewhere | **Fix Win32: cache DIB section** (P0) |
| Frame pacing | Hard-coded `delay(16)` after present (real FPS < 60) | App-side `time`/`sleep` (with a buggy example) | **Adopt time/sleep, correct remaining-time math, configurable FPS** (P0) |
| Resize / DPI | None; also window created with outer size → framebuffer clipped | None; also outer-size bug; no DPI awareness | **Exceed the reference: fix client sizing, DPI, add resize event** (P0/P2) |
| Close semantics | Consistent `LIBPXL_QUIT` event | Inconsistent: Win32 returns -1, X11 unhandled, macOS terminates process | **Keep ours as the documented contract** |
| Error handling | `run` returns 0/1; backend open returns bool | Almost none | **Improve: error codes + `libpxl_error()`** (P2) |
| Multi-window | No (globals) | No (class named after title; runtime ObjC class re-registration) | **Prepare seam now; full support optional** (P2) |

---

## 2. Concrete defects in the current code (independent of the reference)

1. **Client area is smaller than the framebuffer.** `CreateWindowEx(..., width, height)` treats the size as the *outer* window size. With `WS_OVERLAPPEDWINDOW` + `WS_EX_CLIENTEDGE`, the last rows/columns of the framebuffer are clipped. Must use `AdjustWindowRectEx`/`AdjustWindowRectExForDpi`.
2. **Second `libpxl_run()` in the same process fails.** `RegisterClassEx("libpxl")` returns 0 with `ERROR_CLASS_ALREADY_EXISTS`, so `libpxl_backend_open` fails. The class is never unregistered on close either.
3. **Uninitialized first frame.** `libpxl_run` mallocs the buffer, then `libpxl_backend_open` calls `ShowWindow`+`UpdateWindow`, which synchronously fires `WM_PAINT` and blits uninitialized heap memory.
4. **Key mapping is nearly empty.** Only Esc, arrows, `0-9`, `A-Z`; no Enter/Tab/Backspace/Delete/Insert/Home/End/PgUp/PgDn/F1-F12, no punctuation, no modifiers, no auto-repeat flag.
5. **No focus handling.** Alt-Tab while a key/button is held leaves it stuck down forever.
6. **Event queue (32) drops events silently** and mouse-move floods it; clicks/keys can be lost under fast motion.
7. **Hard-coded 60 FPS pacing is wrong**: `draw + present + Sleep(16)` gives < 60 FPS and drifts; no way to choose FPS.
8. **No resize/DPI awareness**: blurry or mis-scaled on high-DPI displays; `CS_VREDRAW|CS_HREDRAW` repaints full window on resize with no scale policy.
9. **No mouse wheel**, no X1/X2 buttons, no cursor tracking outside the client area.
10. **Per-frame GDI object churn** (DC + bitmap created/destroyed every frame) — measurable CPU cost and flicker; `WM_ERASEBKGND` is not handled.

---

## 3. What to adopt from the reference (and why)

**Adopt / adapt**

- **App-owned loop + `time`/`sleep` helpers.** The most valuable idea: expose `libpxl_time()` (monotonic ms) and `libpxl_sleep()`, and let the app own pacing. We keep `libpxl_run()` as a convenience wrapper so existing code and the demo don't break.
- **Polling state *in addition to* events.** `keys[256]`, `mod`, `x`, `y`, `mouse` is what makes the reference pleasant and FFI-friendly. We add query functions while keeping our edge-event callbacks (the reference's biggest usability gap is no press/release edges).
- **Modifier bitmask on every event** (ctrl/shift/alt/super) — cheap, hugely useful, and currently missing.
- **Table-driven key mapping per backend.** Our current `switch` with 4 cases doesn't scale; a table mapping platform keycodes → libpxl codes is the reference's best input lesson.
- **Assignable framebuffer (optional).** The reference's buffer can point at an app-owned buffer (its Doom port draws straight into the emulator's screen buffer). We can offer the same later via `libpxl_set_pixels()` without changing the default library-owned buffer.
- **No-globals backend state.** The reference stores per-window state in the caller struct (and retrieves it via `GWLP_USERDATA`). We adapt this as an opaque backend instance returned by `libpxl_backend_open` — same benefit, better encapsulation, no `HWND` leaking into the public header.
- **Client-area sizing done right.** The reference actually gets this wrong on Win32 too; we fix it instead (P0).
- **A null/offscreen backend for tests.** The reference has no tests at all; adding one gives us CI-testable core logic.

**Do not adopt**

- Single-header-only layout and lack of build system — our CMake + backend interface scales better to X11/macOS. (Optional amalgamation artifact later, P3.)
- Exposing `HWND`/`Display` in the public struct — leaks platform details to users.
- `const int width/height` with no resize path ever.
- Arrow codes 17..20 — they collide with ASCII control characters (Ctrl+Q = 17); our `0x100+` scheme is strictly better, keep it.
- Inconsistent quit semantics (only Win32 returns negative; X11 close kills the process; macOS calls `NSApp terminate`). Keep our `LIBPXL_QUIT` contract and require every future backend to honor it.
- Silent open failure on X11/macOS.
- Per-frame GDI object creation; `CLOCK_REALTIME` for timing; runtime ObjC class registration/`objc_msgSend` tricks for a future macOS backend (use a proper `.m` file); the reference's frame-pacing example bug (sleeps elapsed, not remaining).

---

## 4. Proposed changes, prioritized

### P0 — correctness fixes, no API break (Win32 backend + core pacing)

1. **Client-area sizing**: compute outer size with `AdjustWindowRectExForDpi` (fallback `AdjustWindowRectEx`).
2. **DPI awareness**: `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)` via `GetProcAddress`, fallback `SetProcessDPIAware()`, called before window creation.
3. **Window class lifecycle**: register once per process; treat `ERROR_CLASS_ALREADY_EXISTS` as success; `UnregisterClass` on close only if we registered.
4. **First-frame garbage**: clear the framebuffer to black before `ShowWindow`/`UpdateWindow` (or drop `UpdateWindow`).
5. **Flicker**: `WM_ERASEBKGND` → return 1.
6. **Cached backbuffer**: create a `CreateDIBSection` once per size and reuse its `memdc`; `present` copies/blits from it. No per-frame DC/bitmap creation.
7. **Correct pacing**: add `libpxl_time()` and `libpxl_sleep()`, both routed through the backend (`libpxl_backend_time()` / `libpxl_backend_delay()`), so DOS can supply the PIT/BIOS-tick clock instead of POSIX `clock_gettime`; `libpxl_run` sleeps the remaining frame time and supports a configurable FPS via `libpxl_set_fps()` (default 60, 0 = uncapped).

### P1 — input model

8. **Full key set**, same encoding as today (printable ASCII as-is, specials ≥ `0x100`): Enter, Tab, Backspace, Delete, Insert, Home, End, PgUp/PgDn, F1–F12, L/R Shift/Ctrl/Alt/Super, punctuation via `VK_OEM_*`. Table-driven per backend.
9. **Modifiers**: `LIBPXL_MOD_SHIFT=1, CTRL=2, ALT=4, SUPER=8`; `mods` field on every event (keyboard *and* mouse); handle `WM_SYSKEYDOWN/UP` for Alt.
10. **Mouse wheel**: new `LIBPXL_MOUSEWHEEL` event with `wheel` notches (remember `WM_MOUSEWHEEL` gives screen coords → `ScreenToClient`); optionally X1/X2 buttons.
11. **Auto-repeat**: `repeat` flag on `LIBPXL_KEYDOWN` (don't fake an up/down pair).
12. **Focus loss**: emit `LIBPXL_FOCUSLOST` / `LIBPXL_FOCUSGAINED` and synthesize `KEYUP`/`MOUSEUP` for everything held (in the core, from the tracked input state), so app state can't stick.
13. **Polling queries**: `libpxl_key_down(key)`, `libpxl_mouse_down(button)`, `libpxl_mouse_x/y()`, `libpxl_mods()`, `libpxl_focused()` — core updates state from every event *before* invoking the user callback.
14. **Queue hardening**: coalesce `MOUSEMOVE` (keep only latest position) and/or enlarge the queue so button/key events aren't dropped by motion floods.

### P2 — lifecycle & API remodel (additive, keeps `libpxl_run` working)

15. **Split lifecycle**: `libpxl_open`, `libpxl_poll`, `libpxl_present`, `libpxl_close`; `libpxl_run` becomes a thin wrapper. Lets apps integrate libpxl with their own outer loop and logic between frames.
16. **Backend context**: `libpxl_backend_open` returns an opaque `libpxl_backend *` instead of writing file-scope statics; every backend call takes it. Removes backend globals, prepares multi-window, makes the null backend trivial.
17. **Error reporting**: `libpxl_open` returns an error code; `libpxl_error()` returns a string; backend exposes `libpxl_backend_error()` for detail.
18. **Resize**: emit `LIBPXL_RESIZE` (new client size). Recommended default policy: keep the framebuffer fixed and present it letterboxed, nearest-neighbor, aspect preserved; optional `libpxl_set_size(w,h)` to reallocate later (document that `libpxl_pixels()` can change). The reference has no resize at all — this is a deliberate improvement.
19. **Optional app-provided buffer**: `libpxl_set_pixels(uint32_t *pixels)` for zero-copy integration with existing buffers (the reference's assignable buffer, minus the footgun of the app forgetting to allocate).

### P3 — platform, packaging, tests

20. **Future backends** using lessons from the reference's *mistakes*: X11 must handle `WM_DELETE_WINDOW`, `ConfigureNotify`, use `CLOCK_MONOTONIC` and a keysym table; macOS should be a real `.m` file with an `NSView` subclass and `CGImage` zero-copy, not runtime ObjC tricks.
21. **Null/offscreen backend** selected by a CMake option + a smoke test (loop N frames headless) and a CI matrix (MSVC/MinGW, later Linux).
22. **Optional amalgamation target** producing a single-header release artifact from the modular sources (best of the reference's vendoring ergonomics, without abandoning the architecture).
23. **Docs**: pixel format `0x00RRGGBB` + little-endian BGRA assumption, main-thread requirement for the event loop, key-code table, and a `LIBPXL_PIXEL(r,g,b)` helper macro.

---

## 5. Proposed API sketch (additive; nothing removed)

```c
/* keys: printable ASCII as-is; special keys >= 0x100 (keeps today's scheme) */
enum { LIBPXL_KEY_ESCAPE = 27, LIBPXL_KEY_SPACE = 32, LIBPXL_KEY_TAB = 9,
       LIBPXL_KEY_ENTER = 13, LIBPXL_KEY_BACKSPACE = 8,
       LIBPXL_KEY_DELETE = 0x100, LIBPXL_KEY_INSERT, LIBPXL_KEY_HOME, LIBPXL_KEY_END,
       LIBPXL_KEY_PAGEUP, LIBPXL_KEY_PAGEDOWN,
       LIBPXL_KEY_LEFT, LIBPXL_KEY_RIGHT, LIBPXL_KEY_UP, LIBPXL_KEY_DOWN,
       LIBPXL_KEY_F1, /* .. F12 */ LIBPXL_KEY_LSHIFT, LIBPXL_KEY_RSHIFT,
       LIBPXL_KEY_LCTRL, LIBPXL_KEY_RCTRL, LIBPXL_KEY_LALT, LIBPXL_KEY_RALT,
       LIBPXL_KEY_LSUPER, LIBPXL_KEY_RSUPER };

enum { LIBPXL_MOD_SHIFT = 1, LIBPXL_MOD_CTRL = 2, LIBPXL_MOD_ALT = 4, LIBPXL_MOD_SUPER = 8 };

typedef enum {
    LIBPXL_QUIT, LIBPXL_KEYDOWN, LIBPXL_KEYUP,
    LIBPXL_MOUSEDOWN, LIBPXL_MOUSEUP, LIBPXL_MOUSEMOVE,
    LIBPXL_MOUSEWHEEL,   /* wheel */
    LIBPXL_RESIZE,        /* x, y = new client size (P2, not emitted yet) */
    LIBPXL_FOCUSLOST,
    LIBPXL_FOCUSGAINED
} libpxl_event_type;

typedef struct {
    libpxl_event_type type;
    int key;      /* KEYDOWN/KEYUP */
    int x, y;     /* mouse/resize, client pixels */
    int button;   /* MOUSEDOWN/UP */
    int wheel;    /* MOUSEWHEEL: notches, + = away from user */
    int mods;     /* LIBPXL_MOD_* on every event */
    int repeat;   /* KEYDOWN: 1 = auto-repeat */
} libpxl_event;

/* lifecycle: split, with the one-liner kept as a wrapper */
int  libpxl_open(const char *title, int width, int height);
int  libpxl_poll(void);      /* 1 continue, 0 quit requested, <0 error */
void libpxl_present(void);
void libpxl_close(void);
int  libpxl_run(const char *title, int width, int height,
                libpxl_draw_fn draw, libpxl_event_fn event); /* unchanged */

/* pacing */
uint64_t libpxl_time(void);  /* monotonic ms */
void     libpxl_sleep(int ms);
void     libpxl_set_fps(int fps); /* 0 = uncapped; default 60 */

/* state queries (valid between polls, updated before event callback) */
int libpxl_key_down(int key);
int libpxl_mouse_down(int button);
int libpxl_mouse_x(void);
int libpxl_mouse_y(void);
int libpxl_mods(void);
int libpxl_focused(void);

const char *libpxl_error(void);

/* optional: app-provided framebuffer */
void libpxl_set_pixels(uint32_t *pixels); /* NULL restores library-owned */
```

Backend contract (P2):

```c
typedef struct libpxl_backend libpxl_backend; /* opaque, defined per backend */

libpxl_backend *libpxl_backend_open(const char *title, int width, int height); /* NULL on failure */
void            libpxl_backend_close(libpxl_backend *b);
int             libpxl_backend_poll(libpxl_backend *b, libpxl_event *event); /* 1 event, 0 empty */
void            libpxl_backend_present(libpxl_backend *b, const uint32_t *pixels, int w, int h);
void            libpxl_backend_delay(int ms);
uint64_t        libpxl_backend_time(void);
const char     *libpxl_backend_error(void);
```

---

## 6. Open decisions

1. **P0 fixes + P1 input** — start here (no API break, demo still compiles)? Recommended.
2. **P2 lifecycle split** — keep `libpxl_run` and add `open/poll/present/close`, or go straight to a handle-based `libpxl_window *` API (multi-window capable, breaking)?
3. **Resize policy** — fixed framebuffer with letterboxed nearest-neighbor presentation (recommended default), or reallocate the framebuffer on resize?
4. **Scope of P3** — null backend + tests/CI and amalgamation: include now or defer until an X11 backend exists?

---

## 7. Implementation status

Implemented: P0 and P1, including the four corrections from the DOS
portability assessment (`dos-portability.md` §1).

- **P0.1–P0.6 (Win32)**: client-area sizing via `AdjustWindowRectExForDpi`
  (fallback `AdjustWindowRectEx`), DPI awareness loaded dynamically
  (per-monitor v2, fallback system-aware), window class registered once per
  process with `ERROR_CLASS_ALREADY_EXISTS` treated as success and
  `UnregisterClass` on close, leftover messages drained on open, framebuffer
  zeroed before the first paint, `WM_ERASEBKGND` handled, and a cached memory
  DC/bitmap reused by `present` and `WM_PAINT`.
- **P0.7 (timing)**: `libpxl_time()`, `libpxl_sleep()` and
  `libpxl_set_fps()`; the clock and the sleep are provided by the backend
  (`libpxl_backend_time()` / `libpxl_backend_delay()`), and `libpxl_run` sleeps
  the remaining frame time with a resync when it falls behind.
- **P1.8–P1.14 (input)**: full table-driven key mapping (letters, digits,
  numpad, OEM punctuation, F1–F12, Home/End/PgUp/PgDn/Ins/Del, left/right
  Shift/Ctrl/Alt, Super), `LIBPXL_MOD_*` on every event including
  `WM_SYSKEY*`, mouse wheel and X1/X2 buttons, `repeat` flag, focus events with
  releases synthesized in the core from the tracked input state, polling queries
  (`libpxl_key_down()` and friends) updated before every callback, and mouse-move
  coalescing with a 64-slot queue.
- **DOS corrections**: library sources are C89 + `<stdint.h>` (enforced by
  `C_STANDARD 90` on the library target and a strict C89 syntax check in
  development), `LIBPXL_PIXEL(x, y)` added and the demo uses it, and the DOS
  platform no-ops are documented in `dos-portability.md` §4.6.
- `LIBPXL_FOCUSGAINED` was added alongside `LIBPXL_FOCUSLOST` so
  `libpxl_focused()` has a defined transition back.
- **P2.15–P2.19**: `libpxl_open` / `libpxl_poll` / `libpxl_present` /
  `libpxl_close` split (with `libpxl_run` kept as a wrapper), an opaque
  `libpxl_backend` instance per window, `LIBPXL_ERR_*` codes plus
  `libpxl_error()`, `LIBPXL_RESIZE` with letterboxed nearest-neighbor
  presentation, and `libpxl_set_pixels()` for an app-provided framebuffer.
- **P3.21–P3.22**: `libpxl_null.c` (test backend) with `tests/test_libpxl.c`
  (run by CTest), the single-header amalgamation (`cmake/amalgamate.cmake` plus a
  compile check of the generated header), and a GitHub Actions Windows workflow.
- **P3.20 (DOS)**: `libpxl_dos.c` implements the backend with VBE (32/24/16/8bpp
  plus mode 13h), DPMI LFB mapping, BIOS keyboard and INT 33h mouse polling,
  PIT/BIOS-tick time and `INT 15h AH=86h` sleeps. Compile- and runtime-verified
  on real DOS with both OpenWatcom 2 and DJGPP (see `dos-portability.md` §8).

Still pending: the X11 and macOS backends, the DOS resize/mode-change path, and
the CI matrix for those platforms.

The exact commands for every check (host, strict C89, DOS build and run, input
injection) are in `docs/verification.md`; the helper scripts live in `scripts/`
and the input probe in `tests/keyprobe_dos.c`.
