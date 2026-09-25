# Windows client build (mingw cross-compile) — SECTION 551 follow-up

Founder real-time, 2026-09-25: "can we add windows client to deadweight 2 artifact releases."
D2's own `CLAUDE.md` and `.github/workflows/ci.yml` previously stated flatly that D2 had no
Windows/Android build surface (a deliberate v0 cut, not an oversight) and that adding one needed
"a real, separate scoping pass" first — this doc, plus the `scripts/build.sh --windows` flag and
the CI `windows` job, is that pass.

## Why this is simpler than DEADWEIGHT's own Windows build

DEADWEIGHT's `dw_gui.exe` needs mbedTLS cross-compiled for mingw (see its own
`docs/WINDOWS_TLS_BUILD.md`) because it does real HTTPS to IDUNA over the public internet.
**D2 doesn't need any of that**: `core/http.h` only ever parses `http://` URLs (its own doc
comment: "https:// is not [recognized]"), and nothing in this repo defines
`PARENA_WITH_TLS` (`core/parena_runtime.h`'s own opt-in TLS gate). So the Windows client links
only against SDL2 and winsock (`-lws2_32`, via `core/net.h`'s existing `_WIN32` shim — ported
verbatim from DEADWEIGHT, already Windows-ready before this pass). No mbedTLS, no CA bundle, no
extra build step.

## One-time setup: an SDL2-mingw dev tree

```bash
wget -q https://www.libsdl.org/release/SDL2-devel-2.28.5-mingw.tar.gz
tar xzf SDL2-devel-2.28.5-mingw.tar.gz
mv SDL2-2.28.5/x86_64-w64-mingw32 ./sdl2_mingw   # or point SDL2_MINGW at wherever you put it
```
(Same tarball/version DEADWEIGHT's own CI already downloads for `dw_gui.exe` — no new dependency
introduced to the monorepo.)

## Build

```bash
sudo apt-get install -y mingw-w64          # x86_64-w64-mingw32-gcc
SDL2_MINGW=./sdl2_mingw ./scripts/build.sh --windows
```

Runs the full normal Linux build/test suite first (unchanged), then cross-compiles
`build/dw2_client.exe` and confirms it's a real `PE32+` binary via `file`.

## Status — honest, not glossed over

**Structurally verified**: `x86_64-w64-mingw32-gcc` builds `dw2_client.exe` clean under this
repo's own `-Wall -Wextra -Werror`, zero warnings, against a real local SDL2-mingw dev tree;
`file` confirms `PE32+ executable (GUI) x86-64, for MS Windows`.

**Not runtime-verified**: this sandbox has no Wine/Windows environment to actually launch the
`.exe` and connect to a real `dw2_server`. `core/net.h`'s `_WIN32` winsock shim is the same code
DEADWEIGHT's own `dw_client.exe`/`dw_server.exe` already ship and run on real Windows today, so
the risk is low, but someone should run a real CI-built `dw2_client.exe` on actual Windows against
a live `dw2_server` before treating it as fully equivalent to the Linux build's own verification
(which the smoke tests in `scripts/build.sh` do cover end to end).

## Release packaging

The CI `windows` job builds `dw2_client_windows.zip` — a FLAT zip (`dw2_client.exe`, `SDL2.dll`,
`PLAY.bat`, no folder prefix) — confirmed byte-for-byte the same layout as DEADWEIGHT's own real,
live `dw_gui_windows.zip` (downloaded and `unzip -l`'d directly, not assumed). SDL2 must ship
alongside the exe since it's linked as a mingw import lib, not statically; `PLAY.bat` is a
double-click launcher. This exact zip is what's attached to the GitHub Release. A separate,
build-tagged copy (`D2_Client_Windows_<build>.zip`, plus a standalone `D2_CONSTRUCT.txt`) is also
uploaded as a workflow artifact, matching the sibling Linux client zips' own collision-safe naming
convention for individual CI-run downloads.

**Found and fixed live, first pass got this wrong**: this job originally attached the bare exe and
`SDL2.dll` as two separate loose release assets instead of one zip — technically functional if a
user put both files in the same folder themselves, but not what "bundle it like DEADWEIGHT" meant,
and a real, worse UX than DEADWEIGHT's own one-download zip. Fixed same day.

Only the client got a Windows build in this pass — `dw2_server`/`dw2_local` stay Linux-only for
now (the founder's ask was scoped to "windows client"; `dw2_local` needs the same SDL2-mingw tree
and would be a cheap follow-on if wanted, `dw2_server` needs `-lpthread`'s mingw equivalent
checked first, not assumed free).
