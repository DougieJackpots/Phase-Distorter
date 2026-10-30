# PlayStation Vita port

This directory tracks the native PS Vita platform work for Phase Distorter.
The desktop targets remain the reference implementation and must continue to
build unchanged.

## Current status

**Port scaffold only — no Vita executable or VPK has been built yet.**

The current application entry point is `cpp/src/desktop_application.cpp`.
The desktop frontend is assembled in `cpp/CMakeLists.txt` from:

- `eb_video`: SDL2 plus desktop OpenGL presentation
- `eb_imgui` and `eb_debug_panel`: SDL2/OpenGL2 settings and debug UI
- `eb_desktop_support`, `eb_desktop`: audio, storage, input and display
- `eb_cpp`: the desktop application entry point

The CPU, SNES bus/PPU, DSP, game session, asset importer and most game logic
are separate libraries. This is the part to preserve and reuse.

## Target architecture

The first Vita build should reuse the existing SDL2 frontend where the Vita
SDL2 port supports the required APIs, and use **vitaGL** as the OpenGL ES /
fixed-function compatibility layer. This is preferable to rewriting the
software framebuffer presenter before establishing a working baseline.

| Area | Initial Vita implementation |
| --- | --- |
| CPU, SNES bus, game session | Existing C++ libraries |
| Video | Existing 256x224 framebuffer and presenter through SDL2 + vitaGL |
| Display | Fixed 960x544 Vita screen; preserve 8:7 game aspect ratio |
| Input | SDL game-controller events backed by Vita controls; map to SNES JOY1 |
| Audio | Existing 32 kHz stereo DSP through SDL audio, validate device format |
| Asset import | Existing ROM validation/importer; user supplies their own ROM |
| Saves/config/assets | Vita user storage under `ux0:data/PhaseDistorter/` |
| Package | Vita ELF -> SELF -> VPK, installed with VitaShell |

The Vita display is 960x544. The game framebuffer remains 256x224; do not
stretch it to fill the screen. Letterbox/pillarbox or offer a deliberate
integer-scaled presentation.

## Build environment

Install VitaSDK and its SDL2 and vitaGL ports. The official toolchain file is
normally available at:

```sh
$VITASDK/share/vita.toolchain.cmake
```

The initial cross-configuration should use the VitaSDK toolchain and disable
host-only tests:

```sh
cmake -S cpp -B build-vita \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DEB_BUILD_TESTS=OFF \
  -DEB_BUILD_FRONTEND=ON
```

This is a target configuration goal, **not a claim that the current tree
already configures successfully for Vita**. The desktop frontend currently
requests desktop OpenGL and builds desktop-specific display/input code.
Those dependencies must be made conditional and replaced or adapted before
the command above can produce a Vita executable.

## Porting sequence

1. **Cross-build the core.** Compile the existing CPU, PPU, DSP, asset and
   session libraries with the VitaSDK C++ toolchain. Fix unsupported library
   or compiler assumptions without changing game behavior.
2. **Bring up SDL2 + vitaGL.** Create a Vita SDL window and GL context, upload
   the existing 256x224 frame, and present it at 960x544.
3. **Input.** Map Vita buttons and analog stick to the project's existing
   SNES JOY1 bit mask. Confirm Start/Select and shoulder-button behavior.
4. **Audio.** Validate SDL audio output, sample format, buffering and timing
   on real hardware; avoid allowing audio callbacks to drive emulation time.
5. **Storage.** Use Vita-safe paths and create the application data directory.
   Keep imported asset packs and SRAM outside the VPK.
6. **Package.** Add Vita metadata, icon, SELF and VPK packaging; install and
   launch through VitaShell.
7. **Hardware verification.** Test both regional asset packs, save/load,
   controller input, audio, suspend/resume and sustained gameplay on a physical
   Vita. Vita3K is useful for smoke tests but does not replace hardware testing.

## Constraints

- Do not commit ROMs, extracted asset packs, save files, or proprietary game
  data. Users import their own supported retail ROM.
- Keep the existing desktop build path and its dependencies intact.
- Do not claim a working VPK, 60 FPS, or hardware compatibility until those
  results have been measured and recorded.
- Prefer small platform-specific changes over broad changes to the game core.
