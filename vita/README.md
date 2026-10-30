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
   `ux0:data/PhaseDistorter/earthbound-us.srm` or
   `ux0:data/PhaseDistorter/mother2-jp.srm` periodically and on clean exit.

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

## Performance diagnostics and LiveArea update

This update always displays each completed session update. It removes the
catch-up rule that could hide many consecutive images on an overloaded Vita.
A late update resets the host pacing backlog; it does not skip simulation or
speed up the game. SDL present-vsync remains disabled, with one NTSC host clock.
SRAM is compared with the last successfully saved contents; unchanged saves
no longer rewrite the file every ten seconds. The ARM clock request is 444 MHz.

Release builds enable link-time optimisation across shared libraries and the
frontend when the SDK passes CMake's IPO check. Unsupported SDKs fall back with
a warning. To disable it for comparison, configure with `-DEB_VITA_LTO=OFF`.
The normal build script already selects Release (including core optimisation).

Each launch replaces `ux0:data/PhaseDistorter/performance.log`. It records the
renderer name/flags and actual ARM clock, followed by five-second summaries:
`game_fps`, `displayed_fps`, late updates, and average/maximum milliseconds in
simulation, audio submission, texture upload, presentation, sleeping and saves.
Simulation includes CPU execution, PPU rendering and DSP synthesis; audio_submit
only measures copying samples into the playback queue. The log is diagnostic,
not a claim of measured speed improvement. Play overworld and battle scenes for
at least 30 seconds, exit, and copy this log back for bottleneck analysis.

The supplied bubble icon is preserved byte-for-byte as a 128x128 indexed 8-bit
PNG. The 840x500 LiveArea background and 280x158 startup gate are opaque indexed
8-bit PNGs. Packaging includes both images and their template. Package version
is now 01.01 and LiveArea content revision is 2. The build script validates PNG
headers, dimensions, CRCs, decompression and template references before compiling.
The title ID and save paths are unchanged. Install the rebuilt VPK as an update;
do not delete the installed application to refresh its artwork.

Validation here: host C++ syntax check with SDL/Vita API declarations, existing
frame-pacer tests, asset validation and whitespace checks. VitaSDK, VPK install,
physical scanout and game performance still require validation on your Vita.
If the LiveArea still errors, record its exact error code/message; valid image
encoding alone does not establish the cause of a device-side failure.

## Allocation optimisation follow-up

The Vita audio handoff now swaps two preallocated sample buffers. It no longer
moves the DSP queue away and allocates a replacement each frame. The original
owning audio API remains available to desktop/export callers. Shared sprite
snapshot assignments reuse vector capacity instead of constructing conditional
vector temporaries. The bounded 30-actor ordering pass uses a fixed array and
stable insertion sort, preserving priority/depth ties without heap scratch.

Validation: 120 generated audio blocks match the existing API sample-for-sample
with zero allocations measured in the recycled production/drain path; original
DSP and audio playback tests pass. 100,000 host actor-order fixtures match the
previous stable sort, including ties, empty lists and 30 actors. Changed renderer,
session and Vita frontend pass host syntax checks (Vita APIs are declaration
stubs). These checks do not measure physical Vita FPS or full-game parity.

## Sampled CPU/PPU/DSP diagnostics (v3)

The log now includes `cpu_hardware_sample_avg_ms`, `ppu_sample_avg_ms`,
`dsp_sample_avg_ms`, their maxima, `core_samples`, `ppu_calls`, and `dsp_calls`.
Only one complete session update in 61 is sampled; the other updates do not
read the clock inside DSP/PPU timing scopes. Categories are exclusive even when
scopes nest. CPU/hardware includes the main CPU, SPC sound CPU, scheduling,
sprite preparation outside scanline drawing, and other unclassified work.
PPU measures visible `render_scanline` calls; DSP measures synthesis and its
sample-queue insertion. Samples are host observations, not emulated clocks.

These sampled timings include profiling overhead and are averages per sampled
session update, not per DSP call or scanline. Use several five-second log lines
from representative overworld and battle play. The sampled averages need not
sum to the all-update `simulation_avg_ms`, because they use different subsets.
If a line has `core_samples=0`, it has no component measurements.

The feature defaults on in Vita builds. To compare without detailed counters:
`cmake -S vita -B build-vita -DEB_VITA_RUNTIME_TIMING=OFF`, then run the normal
build script. Re-enable with `-DEB_VITA_RUNTIME_TIMING=ON`. Desktop builds do not
include these core timing probes by default. Neither setting changes game-clock
policy, instruction execution, audio samples, or frame rendering decisions.

Host validation covers enabled/disabled probes, nested phases, resets and
exception cleanup. DSP regression fixtures also pass with profiling active,
including sample equality and zero allocations in recycled audio handoff.
Vita cross-build and device timings still need checking on your SDK/hardware.

## Isolated timing/stride experiments

These changes live on `vita-timing-stride-experiments`. Without a configuration
file, behavior remains enhanced timing on, stride 1, physical input and SRAM on.
No threshold, multiplier, toolchain ABI or DSP batching changes are included.

The Vita frontend reads `ux0:data/PhaseDistorter/performance.cfg`. Four presets
in `vita/experiments/` compare timing on/off crossed with composition stride 1/2.
Copy one preset to that path and copy your replay as `route.txt` into the same
directory. Script format matches the desktop: each non-comment line contains
`<hardware-frame> <SNES-joymask>`; masks may be decimal or `0x` hexadecimal.
Inputs remain held until the next listed change. Use a route that reaches the
scene under test. A blank/boot-only route does not measure overworld or battle.

Replay presets cold-start without loading or writing SRAM, ignore physical
controller input, run 3600 hardware frames and exit. Save each `performance.log`
under a different name before the next run. Replay-only always disables SRAM,
even if a config requests saving. For manual testing use `replay_only=0`, remove
`input_script`, set `frames=0`, and decide explicitly whether to enable SRAM.
Removing performance.cfg restores defaults. Invalid keys/values cause a setup
error rather than silently choosing a different experiment.

The frontend also accepts `--input-script route.txt --replay-only --frames 3600`,
`--enhanced-timing 0|1`, `--composition-stride 1|2|3`, and `--no-save` if your Vita
launcher supplies arguments. CLI options override the file. These are Vita
options; the desktop launch parser is unchanged.

Diagnostics v4 adds per-update simulation p50/p95, over-budget count/fraction,
composed/skipped hardware-frame counts and composed FPS. `displayed_fps` counts
SDL presents, including repeated pictures; it is not the number of unique
pictures at stride 2. Percentiles use actual simulation update times (up to 512
updates per five-second report), not percentiles of window averages. Core phase
samples remain one update in 61 and include measurement overhead.
`ppu_state_sample_*` covers scanline setup and flags-only work on skipped rows;
`ppu_compose_sample_*` includes sprite pixel sampling/overflow on composed rows,
composition, and presentation capture. Sprite overflow on composed rows is not
separately timed because that would require another OAM pass. Remaining PPU
scope overhead stays in the total PPU counter.

Stride 2 composes hardware frame indices 0,2,4,..., keeps scene preparation and
OAM-upload handlers running, and retains range/time-over evaluation on skipped
scanlines. HDMA, IRQs, CPU/SPC/DSP clocks and sample generation continue normally.
The last complete picture is presented on skipped ticks. Skipping is rejected
with widescreen, flash-reference effects or direct-scene rendering enabled.
Fixed-stride output may miss alternating-frame visual effects; adaptive skipping
is not implemented. Host composition settings/counters are excluded from machine
snapshots and reset to stride 1 when loading a snapshot.

Disabling enhanced timing removes the existing extra entity CPU allowance; it
retains the Vita frontend's native sprite runtime and ActorFrames policy. It is
not a promise of exact original-SNES gameplay. Timing modes can diverge in game
state, so compare scene distributions rather than matching their frame hashes.

### Correctness comparison before relying on stride 2

The optional host target `composition_stride_differential` runs two cores from
one asset pack with identical input and timing settings. It compares complete
serialized machine states with only native framebuffer bytes normalized, PCM,
and composed pictures at their original indices. It checks that skipped frames
hold the previous picture. With `EB_GAMEPLAY_AUDIT=ON`, it additionally compares
ordered CPU/DMA/HDMA bus access vectors. Without audit it reports that this check
was not built; run both builds to cover the ordinary native-batching path too.

Configure the host C++ project with frontend off, tests on, checked-in generated
sources and (for the audit run) `-DEB_GAMEPLAY_AUDIT=ON`. Build and run:

```sh
cmake --build build-host --target composition_stride_differential
build-host/composition_stride_differential --assets /path/game.ebpak \
  --input-script /path/route.txt --replay-only --frames 3600 --stride 2 \
  --enhanced-timing 1
```

Repeat with `--enhanced-timing 0`, both audit settings, and representative US/JP
routes including battle swirls/PSI. `--native-sprites 0` additionally tests hardware
OAM rendering. A mismatch is a blocker to trusting skipped composition.

Validation available here is limited to host syntax checks, strict option and
composition-policy tests, runtime timing tests, and 4096 randomized hardware-OAM
fixtures comparing flags-only against composed overflow results. No user game
asset pack or VitaSDK is available here, so the full replay differential, VPK
cross-build and physical-Vita performance have NOT been run. This is an opt-in
experiment, not an established 60-game-tick result.

Diagnostics v4 also logs the CMake configuration and optimisation probes compiled
inside the session and DSP translation units. The older `compiler_optimisation`
field describes only the frontend and is insufficient to identify an optimised
core build. The new probes likewise do not prove flags for every generated CPU
translation unit; use verbose build commands for a complete flag audit.

### Clean measurement before further optimisation

All four replay presets set `core_timing=0`. This disables per-call timer reads
while retaining whole-update simulation timing and instruction/clock counters.
For the strongest comparison, compile with `EB_VITA_RUNTIME_TIMING=OFF` as well;
that removes the per-call probe branches entirely. Re-enable runtime samples by
setting `core_timing=1` only in a build with `EB_VITA_RUNTIME_TIMING=ON`.

Logs include main and SPC instructions and master clocks per hardware frame,
native batch counts, and `simulation_ns_per_main_instruction`. This last field
is whole-simulation nanoseconds divided by main-CPU source instruction count. It
includes SPC, DSP, PPU and scheduling; it is NOT a direct measurement of the
cost of executing one CPU instruction and has no established tens-of-ns target.
The counter represents source/translated instructions rather than host ARM
instructions. Use timing-off/stride changes and comparable scene distributions
to interpret it. Do not infer CPU instruction-cache misses from this ratio alone.

The tile-row cache is scanline-local: HDMA scroll updates between scanlines do
not invalidate cache hits within one scanline. Persistent multi-line caching
would need correct VRAM/register invalidation. RGB565 would change the current
32-bit presentation contract, and DSP batching must preserve shared RAM/echo and
register ordering. Those structural changes are deliberately excluded here.
