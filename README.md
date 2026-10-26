# libpxl
A simple (to-be-crossplatform, win32 for the moment) framebuffer library.

## Building

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

A DOS build (OpenWatcom 2 or DJGPP) and the checks that guard the library are
described in `docs/verification.md`:

```sh
./scripts/dos_build.ps1 -Toolchain watcom -Target smoke
./scripts/dos_run.ps1 -Target smoke -Check
```

## Documentation

- `docs/architecture-review.md` — architecture review and implementation status.
- `docs/dos-portability.md` — DOS portability assessment and findings.
- `docs/verification.md` — how to reproduce every check.