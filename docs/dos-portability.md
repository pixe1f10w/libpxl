# DOS portability assessment (OpenWatcom 2 / DJGPP)

Status: implemented and verified. The DOS backend exists (`libpxl_dos.c`) and the
P0/P1 corrections are in (see `architecture-review.md` §7). Verified on real DOS
(DOSBox) with OpenWatcom 2: compile, link, run, VBE 32bpp mode, LFB blit, and
text mode restore all work. The DJGPP path is written but not yet compiled.
Date: 2026-10-24
Companion to: `architecture-review.md`

Question: can libpxl act as a compatibility layer that lets apps port between
platforms, including DOS (OpenWatcom 2 and/or DJGPP)? Which parts of the proposed
architecture survive, which must change?

---

## 1. Verdict

The proposed architecture (backend split, canonical `0x00RRGGBB` framebuffer,
event callbacks + state queries, `open/poll/present/close`, `time`/`sleep`) is
**compatible with 32-bit DOS**, with four required corrections:

1. **DOS support means 32-bit protected mode.** 16-bit real mode cannot host a
   1.2 MB framebuffer (640×480×4) with a single pointer. OpenWatcom 2 must be used
   as `wcc386 -bt=dos` (32-bit), not `wcc` (16-bit). DJGPP is 32-bit flat by
   construction.
2. **Timing cannot be `clock_gettime(CLOCK_MONOTONIC)` "elsewhere".** DOS is not
   POSIX. The backend must provide the clock and the sleep: this is exactly what the
   proposed `libpxl_backend_time()` / `libpxl_backend_delay()` hooks are for.
   They become load-bearing, not optional.
3. **Library sources must stay C89 + `<stdint.h>`**, because OpenWatcom 2 defaults
   to C89 and its C99 mode is explicitly partial. The current library sources are
   already almost C89-clean; keep them that way and add a compile check. The demo
   may use C99.
4. **Pixel indexing must not use `int` arithmetic** (`y * width + x`), because
   `int` is 16-bit under Watcom 16-bit and the product overflows. Add a
   `LIBPXL_PIXEL(x, y)` macro using `size_t` and document it; fix the demo.

Everything else in the proposal (event callbacks + polling state, canonical key
codes, split lifecycle, opaque backend instance, error reporting, resize event,
app-provided buffer) maps cleanly onto DOS.

---

## 2. Toolchain facts

### OpenWatcom 2

| Item | Fact |
|---|---|
| C standard | C89/C90 is the **default**. C99 is opt-in via `-zastd=c99` (older `-za99`) and is explicitly partial. No C11. |
| `<stdint.h>` | **Available**, including for 16-bit DOS targets (`uint32_t` = `unsigned long`, `uint64_t` = `unsigned long long`). |
| C99 features | `//` comments, mixed declarations, `long long`, designated initializers, `inline`, variadic macros: yes. `for (int i...)` block scoping: unverified ("???" in the official C99 compliance table) — avoid in library sources. VLAs and compound literals: not implemented. |
| DOS targets | 16-bit real mode: `wcc -bt=dos`. 32-bit protected mode: `wcc386 -bt=dos` + `wcl386 -bt=dos -l=dos4g` (or `-l=pmodew`, `-l=dos32a`, `-l=causeway`). |
| Extenders | DOS/4GW, PMODE/W, DOS/32A and CauseWay **ship with OpenWatcom 2**. No separate download. |
| Cross-compiling | First-class: host-specific packages (Linux/macOS/Windows); only `INCLUDE=$WATCOM/h` must be set. |
| 16-bit limits | `int` = 2 bytes, `size_t` = 16-bit, near objects ≤ 64 KB, far objects cannot straddle a 64 KB boundary. >64 KB needs `halloc()` + `__huge` pointers (element size must be a power of two) — not practical for a 1.2 MB framebuffer. |
| Struct packing | Default `-zp2` (16-bit) / `-zp8` (32-bit). BIOS/VESA structs need `#pragma pack(1)`. |
| Interrupts | `<i86.h>`: `int386`/`int386x` (32-bit), `int86`/`int86x` (16-bit); `<bios.h>`, `<conio.h>`, `<dos.h>`, `<dpmi.h>` all present. |
| Build | `wmake` ships. CMake supports DOS cross builds since 3.18: `-G "Watcom WMake" -D CMAKE_SYSTEM_NAME=DOS -D CMAKE_SYSTEM_PROCESSOR=x86`, using `Modules/Platform/DOS-OpenWatcom.cmake` (32-bit links `system dos4g`). Requires `CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY` because test binaries cannot run on the host. |

### DJGPP

| Item | Fact |
|---|---|
| Version | DJGPP 2.05 (`djdev205`); GCC 14.2.0 current, 12.2 widely shipped. |
| C standard | Whatever GCC supports: C99/C11/C17 all work. `<stdint.h>` provided by GCC for `*-msdosdjgpp`. |
| Memory | Flat 32-bit; `malloc` of any size (1.2 MB buffer is fine); `memcpy` to mapped LFB. |
| DPMI | DOSBox provides a built-in DPMI host (no CWSDPMI needed); real DOS uses CWSDPMI. |
| LFB mapping | DPMI `INT 31h AX=0800h` + LDT descriptor (`AX=0000h/0007h/0008h`); DJGPP wrappers `__dpmi_physical_address_mapping`, `__dpmi_allocate_ldt_descriptor`, `__dpmi_set_segment_base_address/limit` (canonical FAQ 18.7 recipe). `__djgpp_map_physical_memory()` is DPMI 1.0-only and can fail under DPMI 0.9. Physical mapping is only reliable for addresses ≥ 1 MB; the LFB qualifies, mode 13h's `0xA0000` does not (use `__djgpp_nearptr_enable()` + `__djgpp_conventional_base` there). |
| Timing | `uclock()` (64-bit, ~840 ns, `UCLOCKS_PER_SEC`), `_bios_timeofday`, `kbhit()` in `<pc.h>`, `getch()` in `<conio.h>`, `ctrlbrk()` in `<dos.h>`. |
| Cross toolchain | Yes: `build-djgpp` prebuilt tarballs (GCC 12.2), `jwt27/build-gcc` (GCC 14.2, djgpp 2.05). Build on Linux/Windows CI, test the `.exe` in an emulator. |

**Recommendation:** DJGPP as the primary DOS backend, OpenWatcom 2 32-bit as the
secondary, sharing one `libpxl_dos.c` with small toolchain shims
(`__DJGPP__` vs `__WATCOMC__`) for: interrupt calls (`int386`/`__dpmi_int`),
physical mapping (DPMI 0800h + LDT descriptor), and timing (`uclock` vs
`_bios_timeofday` + PIT latch read).

---

## 3. Compatibility of the proposed API/architecture, item by item

| Proposal | DOS verdict |
|---|---|
| P0.1 client-area sizing (`AdjustWindowRectEx`) | Windows-only; no-op on DOS. Keep backend-internal. |
| P0.2 DPI awareness | No-op on DOS (no DPI concept). Keep backend-internal. |
| P0.3 window class lifecycle | No-op on DOS. |
| P0.4 clear framebuffer before first present | Compatible; also required on DOS (no uninitialized flash on screen). |
| P0.5 `WM_ERASEBKGND` | Windows-only. |
| P0.6 cached backbuffer + blit | **Adopt on DOS**: offscreen buffer + `memcpy` to the LFB is the standard and safest DOS presentation path. |
| P0.7 `libpxl_time()`/`libpxl_sleep()` + configurable FPS | **Adopt, with a correction**: `CLOCK_MONOTONIC` does not exist on DOS. Timing must be backend-provided (PIT latch read + BIOS tick, or `uclock()`); sleep via `INT 15h AH=86h`. Default 60 FPS + `libpxl_set_fps(0)` uncapped fits DOS. |
| P1.8 full key set, ASCII + `0x100+` | Compatible; BIOS scan codes map directly to the scheme. Needs a per-backend table (as proposed). |
| P1.9 modifiers bitmask | Compatible; BIOS shift flags at `0040:0017h`/`0040:0018h` map to `LIBPXL_MOD_*`. No Super key on DOS. |
| P1.10 mouse wheel | Compatible; INT 33h AX=0003h returns the CuteMouse wheel counter in `BH` (driver-dependent), or via the AX=000Ch event handler with the wheel bit. |
| P1.11 auto-repeat flag | Compatible; BIOS typematic repeat is observable, but see §4.3 — DOS delivers no key-up events by default. |
| P1.12 focus loss → release keys | **Not applicable**: DOS has no focus. Omit; document that DOS is always "focused". |
| P1.13 polling queries | Compatible and needed; state is updated by the backend handlers. |
| P1.14 coalesce/queue hardening | Compatible; the DOS backend's queue is fed from an INT 9 handler, so it must be bounded and lock-free-ish (interrupt-safe). |
| P2.15 split lifecycle | Compatible; on DOS `open` = set video mode + install handlers, `close` = restore mode + remove handlers. |
| P2.16 opaque backend instance | Compatible; DOS needs one static instance because interrupt handlers cannot receive a context pointer (single window per process). |
| P2.17 error reporting | **More important on DOS**: no VBE, no LFB, no suitable mode, no DPMI, no memory. |
| P2.18 resize + letterbox policy | Compatible; `LIBPXL_RESIZE` never fires (fullscreen). `libpxl_set_size` may map to a VESA mode change (optional) or return an error. |
| P2.19 app-provided buffer | Compatible; useful on DOS for apps with fixed memory budgets. |
| P3.20 future backends | Compatible; the DOS backend is the next one. |
| P3.21 null backend + tests/CI | **Adopt**: host tests plus a DOS smoke test. |
| P3.22 amalgamation | Neutral; no DOS impact. |
| P3.23 `LIBPXL_PIXEL` macro | **Promote to P0/P1**: required for correctness under 16-bit `int` and for app portability. |
| — new: C89 policy for library sources | Required for OpenWatcom 2 default mode. |
| — new: documented no-ops | Required for a credible compatibility layer: title, DPI, focus, windowed mode. |
| — new: quit semantics | DOS has no close button; synthesize `LIBPXL_QUIT` from Alt+X / Ctrl+C / Ctrl+Break. |

---

## 4. Concrete proposals

### 4.1 Language and types (portability baseline)

- Library sources (`libpxl.c`, `libpxl_backend.h`, backends) stay **C89 + `<stdint.h>`**:
  declarations at block start, `/* */` comments, no VLAs, no compound literals, no
  `for (int i...)`. This compiles under OpenWatcom 2 default mode and under GCC.
- The demo may use C99. Add a CI compile of the library sources with OpenWatcom 2 in
  default (C89) mode to prevent regressions.
- Add `LIBPXL_PIXEL(x, y)` to the public header:

  ```c
  /* canonical framebuffer: 0x00RRGGBB, row-major, top-down */
  #define LIBPXL_PIXEL(x, y) (libpxl_pixels()[(size_t)(y) * (size_t)libpxl_width() + (x)])
  ```

  Document that apps must index with `size_t`/`LIBPXL_PIXEL`; update the demo.
- Keep the public event struct to `int` fields (coordinates < 32767 are safe even
  on 16-bit), but document that `uint32_t`/`size_t` is the portable type for
  pixel math.

### 4.2 Timing

- Keep `libpxl_time()` (monotonic ms, `uint64_t`) and `libpxl_sleep(int ms)`;
  route both through the backend (as already proposed).
- DOS backend implementation:
  - Clock: latch the PIT counter (`out 0x43,0x00`; read `0x40` low then high) at
    ~838 ns resolution, and count the ~55 ms wraparounds against the BIOS tick
    (`0040:006C`, INT 1Ah) to form a monotonic 64-bit ms clock. `uclock()` is an
    acceptable DJGPP-specific alternative; it reprograms PIT channel 0, so prefer the
    latch-read + BIOS tick combination for portability with OpenWatcom 2.
  - Sleep: `INT 15h AH=86h` (CX:DX µs) with retry on CF set; fall back to a
    PIT-counter busy-wait. Never reprogram PIT channel 0 for pacing (it changes
    DOS's own clock and DOSBox timing).
- `libpxl_run` keeps the corrected remaining-time sleep; `libpxl_set_fps(0)` is the
  DOS "run as fast as the machine allows" mode.

### 4.3 Video mode selection and presentation

- Enumerate, never hardcode: `INT 10h AX=4F00h` (write `"VBE2"` into the buffer
  first for VBE 2.0 data) then `AX=4F01h` per mode from the mode list.
- Prefer a **32bpp linear framebuffer** mode: its memory layout (little-endian
  B,G,R,X) is byte-identical to the canonical `0x00RRGGBB` framebuffer, so
  `present` is a plain `memcpy` with no conversion.
- Fallbacks, in order:
  1. 24bpp LFB: convert (B,G,R byte order) and honor `BytesPerScanLine` pitch.
  2. 16bpp LFB: `(r>>3)<<11 | (g>>2)<<5 | (b>>3)` (verify masks/positions
     from `4F01h`; do not assume 5:6:5).
  3. 8bpp LFB: set a palette (`0x3C8`/`0x3C9`, or VBE `4F09h`) and convert to
     indices (e.g. 3-3-2 or a 6×6×6 cube).
  4. Mode 13h (`INT 10h AX=0013h`, `0xA0000`, palette): only 320×200 — the last
     resort for classic hardware.
- Set the mode with `AX=4F02h`, `BX = mode | 0x4000` (LFB bit); check
  `ModeAttributes` bit 7 (LFB) and bits 0/3/4 (supported, color, graphics).
- Map the LFB with DPMI `INT 31h AX=0800h` + LDT descriptor (`AX=0000h`,
  `AX=0007h` set base, `AX=0008h` set limit), then write through a flat 32-bit
  pointer. `PhysBasePtr` comes from `4F01h`; mapping is only valid for physical
  addresses ≥ 1 MB (the LFB qualifies).
- `present` = `memcpy` from the library framebuffer into the mapped LFB, honoring
  pitch. Optional page flip via `4F07h` when `NumberOfImagePages > 0`.
- Optional vsync: poll port `0x3DA` bit 3, or `4F07h BL=80h` (VBE 2.0+). Time-based
  pacing remains the default.
- `close`: save/restore the previous mode (`4F03h` / `4F04h` state save-restore),
  or at minimum restore text mode (`INT 10h AX=0003h`).

### 4.4 Input

- **Key up/down edges**: INT 16h polling cannot deliver key releases. To honor the
  existing `KEYDOWN`/`KEYUP` contract, the DOS backend should hook `INT 9`
  (keyboard IRQ): read scan code from port `0x60`, bit 7 = release, map set-1 scan
  codes to libpxl key codes via a table, and chain to the previous handler (which
  sends EOI). The handler only writes into a bounded ring buffer; `libpxl_poll`
  drains it. A polling fallback (INT 16h AH=01h/11h + AH=00h/10h) can only deliver
  key-down events.
- **Modifiers**: read BIOS shift flags at `0040:0017h` (and `0040:0018h` for
  left/right Ctrl/Alt) and map to `LIBPXL_MOD_SHIFT/CTRL/ALT`; no Super on DOS.
- **Auto-repeat**: BIOS typematic repeat is visible as repeated make codes; set the
  `repeat` flag on the resulting `KEYDOWN` instead of synthesizing up/down.
- **Mouse**: poll INT 33h AX=0003h (position + buttons) each frame and diff for
  edges; call AX=0000h once at startup, AX=0001h to show the cursor. Wheel via the
  CuteMouse extension (AX=0011h detects it; `BH` in AX=0003h carries the counter)
  or the AX=000Ch handler with the wheel bit. Event-handler callbacks require a
  real-mode callback and locked code/data; polling is the recommended default.
- **Quit**: there is no close button. Synthesize `LIBPXL_QUIT` from Alt+X and from
  Ctrl+C/Ctrl+Break (install an `INT 23h` handler that sets a quit flag, disables
  extended break checking with `INT 21h AX=3301h DL=00h`, and restores the video
  mode before terminating). Document the policy so apps can rely on it.
- **No focus events**: DOS has no focus; do not emit `LIBPXL_FOCUSLOST`; report
  `libpxl_focused()` as always true.

### 4.5 Memory and buffers

- Library-owned buffer: `malloc(1.2 MB)` works under DJGPP and OpenWatcom 32-bit.
  Check for failure and report a clean error.
- The DOS backend keeps one static instance (single window per process); this is
  required because interrupt handlers cannot receive a context pointer.
- Optional back buffer for page flipping doubles the memory footprint; only allocate it
  when `NumberOfImagePages > 0`.
- Do not offer zero-copy direct LFB drawing: writes to uncached LFB memory are slow
  and DOS has no write-combining; the offscreen buffer + `memcpy` is the fast path.
- `libpxl_set_pixels` (app-provided buffer) remains compatible and useful.

### 4.6 Documented platform differences (compatibility-layer contract)

The DOS backend must document these no-ops/differences so ported apps don't rely on
them:

| Feature | DOS behavior |
|---|---|
| Window title | Ignored (no-op). |
| Window decorations / DPI | None; fullscreen graphics mode. |
| `LIBPXL_RESIZE` | Never emitted; `libpxl_set_size` may change the VESA mode or fail. |
| `LIBPXL_FOCUSLOST` | Never emitted; always focused. |
| Letterbox presentation | Not applicable (no window; the mode matches the framebuffer). |
| Mouse position outside window | Not applicable; position may leave the mode's range (clamp or report as-is; document). |

Optional capability query for portable apps (P3): a small bitmask
(`LIBPXL_CAP_RESIZABLE`, `LIBPXL_CAP_WHEEL`, `LIBPXL_CAP_FULLSCREEN`, …) so an
app can adapt its UI at runtime instead of guessing from the platform.

---

## 5. Build and CI

Implemented in the repository (see `docs/verification.md` for the exact
commands):

1. `scripts/dos_build.ps1` downloads the toolchain on first use and builds a
   target for DOS:
   - OpenWatcom 2 (primary): `wcc386 -bt=dos` and `wcl386 -l=dos4g`, with
     `DOS4GW.EXE` copied next to the executable.
   - DJGPP (secondary): the MinGW-hosted cross toolchain with
     `-Wall -Wextra -Wpedantic -std=gnu99`, with `CWSDPMI.EXE` copied next to
     the executable.
   - DOS executable names stay within 8.3 (`DEMO.EXE`, `SMOKE.EXE`,
     `KEYPROB.EXE`).
2. `scripts/dos_run.ps1` runs a target in DOSBox 0.74, checks the result files
   (`-Check`) and captures the emulated screen (`-Capture`), because DOSBox does
   not pass the guest exit code to the host.
3. `scripts/dos_input_check.ps1` checks the input path with DOSBox-X `ADDKEY`
   injection: Esc (app callback) and Alt+X (backend policy) quit the demo, and the
   input probe records the keys and the shift flags. Alt must be held with the
   `l<duration>` form.
4. `scripts/c89_check.ps1` enforces §4.1 with a strict C89 compile
   (`clang-cl /TC /W4 /clang:-std=c89 /clang:-pedantic-errors`).
5. CI: the `windows` job builds and runs the host tests; `c89` runs the strict
   C89 check; `dos` and `djgpp` build for DOS and run the smoke test. Input
   injection stays manual because DOSBox-X is not part of the runner image and
   mouse input has no injection path.

A CMake DOS cross build (`-G "Watcom WMake" -D CMAKE_SYSTEM_NAME=DOS`, with
`CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY`) can replace the OpenWatcom path
of the build script once it is wired into the tree; `LIBPXL_DOS` already selects
the DOS backend in `CMakeLists.txt`.

---

## 6. Risks and open points

1. **Mode enumeration quirks**: never hardcode VBE mode numbers or bpp; DOSBox,
   DOSBox-X and real hardware differ. Enumerate and validate attributes.
2. **32bpp availability**: standard VBE defines up to 24bpp; 32bpp modes are
   vendor/OEM-defined and may be absent on real hardware. The 24bpp/16bpp/8bpp
   fallbacks are required, and the canonical framebuffer conversion path is the
   only portable option.
3. **Interrupt-handler safety**: the INT 9 handler must chain to the previous
   handler, must not call DOS, and must only touch locked, fixed memory. The INT 23h
   handler must restore the video mode. Verify with DJGPP `_go32_dpmi_lock_code/data`
   / `__dpmi_allocate_real_mode_callback` or Watcom equivalents.
4. **Performance budget**: 640×480×4 = 1.2 MB per frame `memcpy` at 60 FPS is
   72 MB/s — fine on modern CPUs and DOSBox, tight on a 386/486. Prefer 32bpp and
   `memcpy`; skip 8bpp conversion unless needed.
5. **DOSBox configuration**: test matrix must cover `machine=svga_s3` (LFB) and
   `vesa_nolfb` (banked fallback), plus `memsize` ≥ 4 MB.
6. **OpenWatcom 2 C99 gaps**: `for (int i...)` scoping is unverified in their
   compliance table; keep library sources C89 to avoid depending on it.

---

## 7. Open decisions

1. Accept "DOS = 32-bit protected mode only" (16-bit real mode unsupported)?
   Recommended.
2. Adopt the four corrections in §1 as part of the P0/P1 scope (timing routed
   through the backend, C89 + `LIBPXL_PIXEL` macro, no-op documentation)?
3. DOS backend priority: DJGPP first, OpenWatcom 2 second (or both from one
   source file with toolchain shims)? Recommended: one `libpxl_dos.c`, both
   toolchains, DJGPP as the primary CI target.
4. Include the DOS smoke test in CI now, or after the X11 backend?

---

## 8. Verification results

Verified on real DOS (DOSBox 0.74-3) with real compilers:

- **OpenWatcom 2** (`wcc386 -bt=dos`, 32-bit DOS): library core, DOS backend
  and the demo compile with 0 warnings and 0 errors; `wcl386 -l=dos4g` links a
  DOS/4GW executable. `dos4gw.exe` must sit next to the executable (it ships in
  `binw` of the distribution).
- **DJGPP** (i586-pc-msdosdjgpp-gcc 12.2, `-Wall -Wextra -Wpedantic
  -std=gnu99`): compiles with no warnings and links. `CWSDPMI.EXE` must sit next
  to the executable because DOSBox 0.74 provides no DPMI host for DJGPP.
- **Runtime smoke test** (`tests/smoke_dos.c`) passes on both toolchains:
  `libpxl_open` returns `LIBPXL_OK`, three frames are drawn and presented, and
  `libpxl_close` restores text mode. DOSBox reports a 320x200 32bpp linear
  framebuffer mode, 1280 bytes per line, physical base 0xC0000000.
- **Input verified**: the demo marker follows the host cursor (INT 33h position
  plus `LIBPXL_MOUSEMOVE` events), Esc quits through the app callback, and
  Alt+X quits through the backend. DOSBox-X injects the held form with
  `ADDKEY p2500 l1000 a-x`; a plain `ADDKEY a-x` releases Alt too quickly for
  the BIOS shift flags, which is why the backend also remembers the Alt key event.
- **DPMI conventions measured on DOSBox**: `INT 31h AX=0100h` returns the value
  the BIOS accepts in `ES` and that the block is readable at in `DX`
  (`linear = DX*16`); `AX=0101h` frees with `DX`. Hosts differ, so the DOS
  backend keeps both values and the VBE controller-info call tries both as the
  segment.
- **8.3 executable names**: DOSBox's shell cannot find names whose base name
  exceeds 8 characters. `smoke_dos.exe` (9 characters) fails silently as
  "bad command or file name" while `smoke.exe` runs. Keep DOS executable names
  within 8.3 (CI must use `SMOKE.EXE`).
- **Interrupt calls**: use `int386x` with `segread()` on OpenWatcom; `intr()` with
  `REGPACK` crashes. On DJGPP, strict `-std=c99` hides the DPMI declarations
  (`__STRICT_ANSI__`); use `-std=gnu99`.

Not yet verified: real hardware (DOSBox only), the 24/16/8bpp fallbacks (DOSBox
picked 32bpp), the keyboard focus/quit handlers (not implemented), and DOSBox-X
and DOSBox Staging behaviour.
