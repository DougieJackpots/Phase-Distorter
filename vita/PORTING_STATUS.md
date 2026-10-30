# Vita port status

Last updated: initial SDL2 frontend implementation on `vita-port`.

## Repository and implementation progress

- [x] Dedicated `vita-port` branch created from `main`; desktop branch untouched.
- [x] Existing C++20 core and desktop frontend boundaries identified.
- [x] Existing SNES framebuffer dimensions identified: 256x224.
- [x] Existing DSP output identified: 32 kHz signed 16-bit stereo.
- [x] Optional `EB_BUILD_VITA` CMake target added; desktop frontend remains independently selectable.
- [x] Dedicated VitaSDK CMake project and build script added.
- [x] Initial SDL2 Vita frontend added: framebuffer presentation, Vita button mapping, audio queue, asset-pack loading, and SRAM persistence.
- [x] Vita build and asset-transfer instructions documented.
- [ ] VitaSDK cross-build and compiler/linker fixes.
- [ ] Verify SDL2 Vita renderer and audio backend behavior.
- [ ] Verify asset-pack and save-file filesystem behavior on device.
- [ ] Verify VPK installation and launch on physical Vita.
- [ ] Profile frame pacing, memory use, audio underruns, and suspend/resume on hardware.

## Current architecture

The Vita frontend deliberately does **not** use the desktop OpenGL/ImGui
frontend or vitaGL. It uses SDL2's 2D renderer to upload the existing 256x224
ARGB framebuffer and preserve its 8:7 aspect ratio on the 960x544 display.
Vita controller input is read through `SceCtrl`; the shared `GameSession`,
asset validation, native sprite runtime, DSP, and audio queue are reused.

The VPK contains the application only. It does not include a ROM or game data.
A user-created `.ebpak` made by the desktop importer must be copied to
`ux0:data/PhaseDistorter/game.ebpak`. SRAM is stored at
`ux0:data/PhaseDistorter/game.srm`.

## Build

With VitaSDK, its SDL2 port, CMake, Python 3, and cc65 (`ca65`/`ld65`)
installed, set `VITASDK` and run:

```sh
bash vita/build.sh
```

The script has not yet been run in a VitaSDK environment. No ELF, SELF, or VPK
has been produced or tested at this point.

## Remaining acceptance criteria

- VPK installs and launches on a physical PS Vita.
- Missing assets produce a useful setup message.
- A supported user-imported asset pack loads correctly.
- Rendering is stable and undistorted at the Vita display resolution.
- All controls map correctly, including face buttons and shoulder buttons.
- Audio plays without underruns or drift during normal gameplay.
- SRAM survives clean exit and relaunch.
- Desktop builds remain unaffected.
