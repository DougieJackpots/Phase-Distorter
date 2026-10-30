# Vita port status

Last updated: initial `vita-port` branch setup.

## Repository assessment

- [x] Dedicated `vita-port` branch created from `main`.
- [x] Existing C++20 core and desktop frontend boundaries identified.
- [x] Existing SNES framebuffer dimensions identified: 256x224.
- [x] Existing DSP output identified: 32 kHz signed 16-bit stereo.
- [ ] VitaSDK core cross-build.
- [ ] SDL2 Vita frontend.
- [ ] vitaGL presentation.
- [ ] Native Vita input mapping.
- [ ] Vita audio verification.
- [ ] Vita user-storage paths.
- [ ] SELF/VPK packaging.
- [ ] Physical Vita test.

## Important implementation findings

The desktop target is not just a thin SDL launcher. Its CMake target links
desktop display/input, SDL audio, an ImGui SDL2/OpenGL2 settings/debug UI and
OpenGL frame presentation. The current CMake frontend also calls
`find_package(OpenGL REQUIRED)`. A Vita build therefore needs an explicit
platform branch; simply changing the compiler target or renaming the desktop
binary will not make a valid Vita port.

The simulation is more portable: `eb_session`, `eb_core`, `eb_audio`,
`eb_dsp`, `eb_assets`, and `eb_native_engine` are separate from the SDL
window and desktop display classes. Reuse these libraries and isolate Vita
platform services from them.

The existing interactive startup uses `SDL_GetPrefPath` and standard
filesystem operations for imported assets and saves. Verify the Vita SDL2
port's preference-path behavior and filesystem support, then explicitly
establish the application's `ux0:data/PhaseDistorter/` directory rather than
assuming desktop paths are valid.

## Acceptance criteria for the first usable VPK

- VPK installs and launches on a physical PS Vita.
- Missing assets produce a useful import/setup message.
- A user-imported EarthBound or Mother 2 asset pack loads successfully.
- The game renders without distortion at the Vita's 960x544 display size.
- Vita controls operate the existing SNES input bit mask.
- Game audio plays without underruns during normal gameplay.
- SRAM persists across clean exit and relaunch.
- Desktop builds remain unaffected.
