# PlayStation Vita port

The `vita-port` branch contains an initial native Vita frontend. The existing
desktop targets remain the reference implementation and are kept separate.

## Current implementation

- Shared C++20 game/session, CPU, native sprite runtime, asset validation and
  DSP libraries.
- SDL2 2D renderer uploads the native 256x224 ARGB framebuffer. The image is
  scaled with aspect preservation to the Vita's 960x544 display.
- Vita `SceCtrl` input is translated to the existing SNES JOY1 bit layout.
- Existing SDL audio queue plays the 32 kHz stereo DSP output.
- User asset packs and SRAM live in
  `ux0:data/PhaseDistorter/`, outside the VPK.
- VitaSDK CMake packaging produces a SELF and VPK.

This first frontend intentionally avoids desktop OpenGL, vitaGL, and ImGui.
Desktop menus, debug UI, screenshots, save-state snapshots, and desktop
preferences are not part of this first Vita baseline.

## Build environment

Install VitaSDK, its SDL2 port, CMake, Python 3, and cc65 (the `ca65` and
`ld65` tools). Set `VITASDK` to the SDK installation root and run:

```sh
bash vita/build.sh
```

The script configures `vita/CMakeLists.txt` with the Vita toolchain, disables
the desktop frontend and test suite, regenerates the translated C++ sources
from the assembly checkout, builds the Vita executable, and packages
`PhaseDistorter.vpk`.

The SDL2 lookup expects the SDK's standard
`arm-vita-eabi/include/SDL2` and `arm-vita-eabi/lib` locations when no
SDL2 CMake package is found.

## Game data and saves

The VPK does not contain a ROM or extracted game data.

1. On a desktop build, use the asset importer with your own supported
   EarthBound or Mother 2 ROM.
2. Copy the resulting user-created asset pack to
   `ux0:data/PhaseDistorter/game.ebpak` on the Vita.
3. Launch the installed bubble. SRAM is written to
   `ux0:data/PhaseDistorter/earthbound-us.srm` or\n   `ux0:data/PhaseDistorter/mother2-jp.srm` periodically and on clean exit.

The original ROM is not packaged or modified.

## Controls

| Vita control | In-game input |
| --- | --- |
| D-pad | Direction |
| Cross | A |
| Square | X |
| Circle | B |
| Triangle | Y |
| L / R | L / R |
| Start / Select | Start / Select |

## Validation still required

This is a porting baseline, not a verified release. The VitaSDK build script
has not yet been run in a VitaSDK environment; no ELF, SELF, or VPK has been
produced or tested. SDL2 renderer/audio behavior, compiler/linker compatibility,
filesystem durability, performance, audio underruns, suspend/resume, and actual
hardware input all need testing on a physical Vita. Do not treat the current
source as a proven 60 FPS build.
