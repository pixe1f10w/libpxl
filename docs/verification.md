# Verification guide

How to reproduce every check that guards libpxl, including the DOS port.
Companion to `architecture-review.md` (status) and `dos-portability.md`
(assessment and findings).

Nothing here is needed for a normal build. The toolchains are downloaded into a
scratch directory and never installed system-wide.

---

## 1. Host checks

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

- `libpxl_tests` runs the core against the null backend
  (`libpxl_null.c`), which has deterministic time and an event injection hook.
- The build also generates the single-header amalgamation
  (`cmake/amalgamate.cmake`) and compiles it
  (`libpxl_single_check`).

Strict C89 check (the library must build with OpenWatcom 2's default C89 mode):

```powershell
./scripts/c89_check.ps1
```

The script compiles `libpxl.c`, `libpxl_win32.c` and `main.c` with
`clang-cl /TC /W4 /clang:-std=c89 /clang:-pedantic-errors`. Run it from a
Visual Studio developer shell so `clang-cl` finds the Windows SDK headers; CI uses
`ilammy/msvc-dev-cmd` for the same reason. This check found real C99-isms
(trailing enum commas, a non-constant aggregate initializer, declarations after
statements), so keep it green.

---

## 2. DOS toolchains

Both build `libpxl.c` and `libpxl_dos.c` for 32-bit DOS. A 16-bit build cannot
hold the framebuffer, and DOSBox provides no DPMI host for the extenders, so the
extender binary is copied next to the executable.

`scripts/dos_build.ps1` downloads the toolchain on first use:

```powershell
./scripts/dos_build.ps1 -Toolchain watcom -Target smoke
./scripts/dos_build.ps1 -Toolchain djgpp  -Target smoke
./scripts/dos_build.ps1 -Toolchain watcom -Target demo
./scripts/dos_build.ps1 -Toolchain watcom -Target keyprobe
```

### 2.1 OpenWatcom 2 (primary)

- Installer:
  `https://github.com/open-watcom/open-watcom-v2/releases/download/Current-build/open-watcom-2_0-c-win-x64.exe`
- The installer is NSIS, so `7z x` unpacks it; nothing is installed.
- Environment: `PATH += binnt64`, `LIB = lib386;lib386\dos`,
  `WATCOM = <root>`.
- Compile: `wcc386 -bt=dos -i=<root>\h -w4 -fo=<file>.obj <file>.c`
- Link: `wcl386 -bt=dos -l=dos4g -fe=<name>.exe <objects>` (32-bit DOS/4GW).
- Copy `binw\dos4gw.exe` next to the executable.
- DOS executable names stay within 8.3: `DEMO.EXE`, `SMOKE.EXE`, `KEYPROB.EXE`.

### 2.2 DJGPP (secondary)

- Cross toolchain:
  `https://github.com/andrewwutw/build-djgpp/releases/download/v3.4/djgpp-mingw-gcc1220-standalone.zip`
  (MinGW-hosted, so it runs on Windows).
- Compile and link:
  `i586-pc-msdosdjgpp-gcc -O2 -Wall -Wextra -Wpedantic -std=gnu99 -I. -o <name>.exe libpxl.c libpxl_dos.c <target>.c`
- `-std=gnu99` is required: strict `-std=c99` defines `__STRICT_ANSI__`, which
  hides the DPMI declarations in `<dpmi.h>`.
- CWSDPMI: `http://www.delorie.com/pub/djgpp/current/v2misc/csdpmi7b.zip`;
  copy `bin\CWSDPMI.EXE` next to the executable.

---

## 3. Running on DOS

DOSBox 0.74-3 (windowed, for rendering and mouse checks):

```powershell
./scripts/dos_run.ps1 -Target smoke -Check
./scripts/dos_run.ps1 -Target demo -Capture -Shot build-dos/demo-dos.png
```

The command line is:

```
dosbox -c "mount c <dir>" -c "c:" -c "<NAME>.EXE" -c "exit" -exit
```

- The guest writes its own result files (`SMOKE1.TXT`, `SMOKE2.TXT`,
  `SMOKE3.TXT`), because DOSBox does not pass the guest exit code to the host.
- `-Check` verifies `SMOKE2.TXT = 0` (open succeeded) and
  `SMOKE3.TXT = 3` (three frames drawn and presented).
- `-Capture` runs windowed and copies the emulated screen from the DOSBox window,
  which is how the demo rendering is compared against the host build.
- The DOSBox window must be given a moment to appear before it is captured; the
  screen is exactly the requested size (`640 x 480` for the demo).

DOSBox-X (key injection, for the input checks):

```powershell
./scripts/dos_input_check.ps1
```

- DOSBox 0.74 cannot inject keys, so DOSBox-X is used with `ADDKEY`:
  `addkey p2500 escape` and `addkey p2500 l1000 a-x`.
- Alt has to be *held* with the `l<duration>` form: a plain `ADDKEY a-x`
  releases Alt too quickly for the BIOS shift flags, which is why the backend also
  remembers the Alt key event itself.
- The input probe (`tests/keyprobe_dos.c`) records what the guest sees into
  `KEYLOG.TXT` (keys with the classic and extended shift flags, and mouse
  position/buttons). The script checks `scan=1 ascii=1b` for Esc and the Alt bit
  (`classic=..8`) for Alt+X.
- The demo is checked to quit on Esc (the app callback) and on Alt+X (the backend
  policy); DOSBox-X exits through the autoexec `exit`.

Mouse movement is checked with `-Capture`: move the host cursor over the DOSBox
window and the demo marker follows it (INT 33h position plus `LIBPXL_MOUSEMOVE`
events).

---

## 4. CI mapping

| CI job | Steps |
|---|---|
| `windows` | CMake configure, build, `ctest` (host tests and null backend) |
| `c89` | `ilammy/msvc-dev-cmd`, `scripts/c89_check.ps1` |
| `dos` | `choco install dosbox`, `scripts/dos_build.ps1 -Toolchain watcom`, `scripts/dos_run.ps1 -Check` |
| `djgpp` | `choco install dosbox`, `scripts/dos_build.ps1 -Toolchain djgpp`, `scripts/dos_run.ps1 -Check` |

Input injection stays out of CI: DOSBox-X is not part of the runner image, and
mouse input has no injection path. The input checks are a manual step with
`scripts/dos_input_check.ps1`; the null backend covers input state on the host.

---

## 5. Known constraints

- **DOS executable names are 8.3**: `SMOKE_DOS.EXE` is silently not found by the
  DOSBox shell ("bad command or file name") while `SMOKE.EXE` runs. Keep names
  within 8.3.
- **Extenders**: `DOS4GW.EXE` (OpenWatcom) or `CWSDPMI.EXE` (DJGPP) must sit
  next to the executable.
- **Interrupt calls**: use `int386x` with `segread()` on OpenWatcom; `intr()` with
  `REGPACK` crashes under DOS/4GW. On DJGPP the 32-bit register member is `edi`.
- **DPMI 0100h**: hosts disagree on which return value is the real-mode segment
  (DOSBox returns it in `DX`). The DOS backend keeps both values and the VBE
  controller-info call tries both as the segment.
- **Line endings**: the repository files are LF; Git may check them out as CRLF on
  Windows. Neither toolchain cares.
