# MK64 - IntuitionEngine port: engineering notes

Port and this record by Zayn Otley (zayn@duck.com).

Intuition Engine: https://intuitionengine.io

Single record for the IE port. Combines the performance investigation
(formerly `PERF-NOTES.md`) and the M68K coprocessor gfx/audio offload
implementation log (formerly `ie/coproc/GFX_SVC_NOTES.md`). The branch
currently runs at native N64 pace (~30 fps at the attract race, headless and
windowed); 60 fps is the aspirational target and remains open (see Current
state below and Gate J). The 60 fps route is to drop the guest frame below
16.7 ms of emulated work by moving display-list translation host-side over the
command-stream ABI. This file is the durable record for the port; reproducing
an end-to-end run also needs the matching IntuitionEngine build named in the
compatibility manifest below. Any scratch task-list kept elsewhere is
scheduling only and holds no facts this document depends on.

## Current state

Snapshot 2026-07-12. This section is the one-page summary; the dated body
below is the supporting evidence.

- Delivers: MK64 running on the IntuitionEngine multi-core target at native
  N64 pace. The full coprocessor pipeline (main M68020 + two M68020 workers +
  IE64 worker + Voodoo) builds, packs and boots from `make ie-pack`.
- Default services: `IE_TNL`, `IE_GFX_SVC` and `IE_AUDIO_SVC` all default to 1
  (T&L on the IE64 worker, gfx translation on worker A, audio on worker B).
  Clear any flag to drop that stage to its local path on main.
- Performance: attract race ~30 fps headless and windowed, both native N64
  pace, measured poll-free (`race_fps_lite`); the frame is ~30 ms guest-bound
  with worker A the pace-setter. Battery reads ~30, AC/cool ~33-36. The intro
  fly-by pan is heavier (~18-19 fps, gfx-translation-bound), not a regression.
- Verified: boot-and-render smoke (`ie-pack-smoke`) green; the gfx parity
  oracle passed sync and pipelined at the P1 build; audio A/B by ear signed
  off 2026-07-12.
- Unverified / soft: a full visual A/B pass is still recommended; the
  local-vs-service swap-hash oracle is unreliable on the current engine build
  (hashes match at swaps 150/300/450 but diverge from swap ~560), so bit-exact
  gfx parity is not re-confirmable until that oracle is rebuilt.
- Principal limitation: worker A geometry emission (`vd_draw_triangles`, 33%
  of its frame) is the dominant remaining cost; the low-cost guest-C levers
  are exhausted (Gate J standing conclusion).
- Target distinction: the compatibility target reached here is native N64 pace
  (~30 fps). 60 fps is aspirational and is blocked on the next action.
- Next action: a native host geometry/emission kernel over the command-stream
  ABI (SSE/AVX/AVX2, no FMA under the bit-exact regime), gated on first
  rebuilding a deterministic parity oracle. Engine-side project, not a game-C
  patch.

### Minimum validation before merge or ABI change

- `make test` - host unit suite (gfx translate, fast3d, Voodoo, audio, maths).
- `make ie-pack-smoke` - default full-pipeline build boots and renders headless.
- `make ie-gfx-svc-parity IE_GFX_SVC=1` - gfx service hashes match the local
  path, sync and pipelined (only meaningful while the oracle is trustworthy on
  the engine in use).
- `make ie-race-fps-lite` - poll-free race fps A/B; compare medians on an idle
  box.
- Audio A/B by ear against the local path (music, sfx, pitch, timing).

### Required IntuitionEngine build (compatibility manifest)

Sibling checkout `../IntuitionEngine`. Tested against engine `main` at commit
`0e0fe08b` ("Support a second M68K coprocessor worker instance"), which brings
the three commits this branch depends on onto `main`:

- `9472245e` M68K coprocessor worker fixes (stack bounds, JIT arming,
  shared-window I/O pages).
- `71f939c0` deterministic per-swap frame hashing behind `IE_SWAP_HASH=1`.
- `0e0fe08b` second M68K coprocessor worker instance (`COPROC_INSTANCE`).

Build against `main` at or after `0e0fe08b`, plus the mailbox layout revision 1
change (later than that tag; see the mailbox-ABI migration note below).
Concretely the engine must provide:

- Multi-instance coprocessor workers: `COPROC_INSTANCE` reg (0xF238C), second
  M68020 window `WORKER_M68K2` (0x420000), two M68K instances.
- Mailbox layout revision 1: uniform 0x400 ring stride (base 0x790000, ring
  index = cpuTypeToIndex x 2 + instance), a START-time version gate (the worker
  echoes `COPROC_LAYOUT_VERSION` into `ring_base + 0x04` or START fails with
  `COPROC_ERR_STALE_WORKER`), and per-instance liveness in `COPROC_INSTANCE_STATE`
  (0xF25B8, bit `cpuType x 2 + instance`).
- Per-swap deterministic swap-hash (`IE_SWAP_HASH`, regs 0xF8358/5C/60).
- Voodoo command-stream ABI (`VOODOO_CMD_PTR/COUNT/SUBMIT`) and bulk texture
  upload (`VOODOO_TEX_SRC_PTR/BYTES`).
- Compositor frame-generation gate (934d1513), the Gate H texture-path fix
  (a630dabd), swap-path stage timing and perf bindings (b3da54a2), and the
  Gate I backend lock granularity (9e84470f).
- The Phase 0 M68K worker JIT fixes (stack bounds, worker JIT arm, CoprocMode
  I/O pages, concurrent-compile lock).

Known engine caveats at this build (`0e0fe08b`): pre-existing Z80/IE32 worker
races; `TestVoodooMegaDemoBasicRunAOTSmoke` fails independently; the swap-hash
parity oracle is currently unreliable (local-vs-service hashes diverge from
swap ~560).

### Outcome ledger

| item | outcome |
|---|---|
| gfx service (worker A), pipelined | accepted, default on |
| audio service (worker B) | accepted, default on |
| IE64 T&L service | accepted, default on |
| Gate J lever 1 - `emit_vertex` combiner hoist | accepted |
| Gate J lever 2 - Voodoo combiner hoist | accepted, largest win |
| Gate J lever 3 - content-keyed texture cache | rejected (2.8% hit) |
| Gate J follow-up - T&L overlap rewrite | rejected and reverted (~10% slower) |
| Matrix-load reuse | kept as a risk-free no-op |
| Swap pipelining depth >1 (early attempt) | reverted, later enabled at Gate I |
| MMIO to direct-RAM | no lever (already RAM-backed) |
| 60 fps via present-path engine work | exhausted (Gate I) |

---

## Building a runnable IE package

Source ROM (user-supplied, git-ignored, never committed): the US retail
cartridge dump `Mario Kart 64 (USA).z64`, a big-endian `.z64` of exactly
0xC00000 bytes (12 MiB). Place it in the project root (or pass its path via
`ROM=...`). The asset pipeline in `tools/rom_assets.py` accepts only this
image and enforces it by SHA1:

- name: `Mario Kart 64 (USA).z64`
- size: 0xC00000 (12582912) bytes
- sha1: `579c48e211ae952530ffc8738709f078d5dd215e` (the value the pipeline
  checks, `tools/rom_assets.py:39`)
- sha256: `d6b8538dd63f0132ecb2856e7d32816ed3c30e3e479aecd23cf83fb6ba17a5da`
  (recorded for convenience only; computed by `sha256sum` over the
  SHA1-verified image, not enforced by the pipeline)

`make assets ROM="Mario Kart 64 (USA).z64"` extracts the neutral
asset layout into `rom_assets/` (also git-ignored); `make assets-verify`
checks an existing layout with no ROM present.

`make ie-pack` is the canonical build: it produces the self-contained
`build/ie/mariokart64.ie68` that the engine loads directly (`ie-pack-smoke`
runs it headless as a sanity check). The full coprocessor pipeline is the
default: the pack contains the loader, `game.bin`, and all three service
images, because `IE_TNL`, `IE_GFX_SVC` and `IE_AUDIO_SVC` all default to 1.
Each stage can be dropped to its local path on main by setting the flag empty
on the command line:

    IE_GFX_SVC= make ie-pack          # gfx translation on main
    IE_AUDIO_SVC= make ie-pack        # audio pump on main
    IE_TNL= IE_GFX_SVC= IE_AUDIO_SVC= make ie-pack   # everything on main

`IE_GFX_SVC` drives the gfx service (worker A), `IE_AUDIO_SVC` the audio
service (worker B), `IE_TNL` the IE64 T&L service; a stage also falls back to
the local path automatically if its worker fails to start at boot. Keep the
game build hard-float (`-m68881`); do not enable LTO (see the standing rules
below).

## Runtime controls

The IE game target maps keyboard input to controller port 0 only. Click or
focus the engine window first; title-screen progress uses the N64 Start
button, mapped to `Enter`.

| N64 control | Keyboard |
| --- | --- |
| Analogue stick | Arrow keys |
| A | `X` |
| B | `Z` |
| Z trigger | `Space` |
| Start | `Enter` |
| L trigger | `Q` |
| R trigger | `E` |
| C-up | `I` |
| C-down | `K` |
| C-left | `J` |
| C-right | `L` |
| D-pad up | `W` |
| D-pad down | `S` |
| D-pad left | `A` |
| D-pad right | `D` |

Building and running:

    make ie-pack        # build the self-contained mariokart64.ie68 (the ROM to run/ship)
    make ie-pack-smoke  # build it and boot/render headless as a sanity check
    make ie-game-run    # build the multi-file image and launch it in the engine

The packed ROM is a single file the full IntuitionEngine binary loads
directly: pass `build/ie/mariokart64.ie68` as the program image.

## How to read this document

This is a lab record, not a spec of current HEAD. Parts 2 to 4 interleave
history, rejected experiments, and current guidance. Read any figure as a
snapshot of the build named beside it: entries carry a `2026-07-xx` date (and
sometimes an engine or upstream commit hash), and absolute numbers, image sizes and
line references were true at that snapshot and drift as the tree moves. Where
a size or address is given without a commit it is tagged "at the build
recorded here". Present-tense phrasing such as "implemented and building"
means true as of its dated section, not a standing guarantee; re-measure
against the current build before relying on any number.

Engine-side claims (bugs, tests, commits such as 934d1513) refer to the
separate IntuitionEngine emulator repository, a sibling checkout at
`../IntuitionEngine`, not to this game repo; those short hashes resolve only
there, and the cited Go tests and MMIO register addresses live in that tree.

Glossary: BE/LE big/little-endian; ABI application binary interface; DL
display list (the N64 RCP command stream); RCP Reality Coprocessor (the N64's
GPU/DSP); T&L transform and lighting; TU translation unit (one compiled `.c`);
MP modelview-projection matrix; EA effective address (68k addressing mode);
LFB linear frame buffer; SPSC single-producer single-consumer (ring);
PC has two meanings by context: the portable `gfx_pc` layer (Fast3D
display-list interpreter, e.g. "gfx_pc"), and program counter in the
profiling sense ("PC sample", "svc_pc_sample" = statistical program-counter
sampling of a worker); WPn work package n, an ordinal from the original
optimisation plan; MMIO memory-mapped I/O.

---

# Architecture: cores and the division of labour

The IntuitionEngine target this port runs on is a multi-core machine, and the
port uses that fabric deliberately. In the default configuration (all three
services on) four programmable cores plus the fixed rasteriser run in
parallel, split the same way the Nintendo 64 splits work across its R4300 CPU
and the RCP's RSP and RDP.

Cores and what each owns:

| core (ISA) | RAM window | ring | owns | why here |
|---|---|---|---|---|
| main M68020+68881 | game RAM 0x10000000+ | - | game logic, physics, AI, input, and display-list production (the game's render code emits Fast3D display lists), plus audio command production | the deterministic single-threaded simulation that owns game state; must stay one core so state is never raced |
| M68020 worker A | 0x280000-0x300000 | 4 | display-list translation (walk the Fast3D DL, build vertices, clip), Voodoo command submission, frame pacing | the heaviest per-frame CPU cost; on its own core it overlaps with next-frame simulation on main |
| IE64 worker | 0x3A0000-0x420000 | 10 | per-vertex transform and lighting (matrix x vertex, lighting, clip setup) | tight numeric inner loop; the IE64 is the engine's fast 64-bit scalar floating-point core, so it does the vertex maths |
| M68020 worker B | 0x420000-0x4A0000 | 5 | audio sequencer (`process_sequences`), envelopes, pitch, note allocation, and per-note voice-state writes through voice MMIO (the engine does the PCM mixing) | audio is periodic and latency-tolerant, independent of the video frame; a dedicated core lifts ~25% of audio cost off the main frame |
| 3dfx Voodoo | shared-RAM command buffer + texture slots, MMIO doorbell | - | triangle rasterisation, texturing, blending, framebuffer | fixed-function pixel fill and texture sampling; the pixel back-end, not a CPU workload |

A fifth window exists for an x86-64 worker (0x320000-0x3A0000); an x86
worker-JIT T&L path was prototyped and rejected in favour of the IE64 assembly
T&L service, so it is unused in the steady state.

How this maps onto the N64's RCP/RSP/RDP model:

| Nintendo 64 | this port | shared role |
|---|---|---|
| MIPS R4300 CPU | main M68020 | game logic; emits display lists |
| RSP (vector unit running gspFast3D microcode) | IE64 worker + worker A's DL walk | transform, lighting, clip; display-list processing |
| RDP (rasteriser) | 3dfx Voodoo | triangle raster, texture, blend, framebuffer |
| RSP audio microcode (time-shared) | dedicated M68020 worker B (sequencing and voice control) + engine (PCM mix) | note sequencing, envelopes, voice state; final PCM mix |

The N64 CPU builds a display list in RDRAM, the RSP transforms and lights it,
and the RDP rasterises; CPU, RSP and RDP overlap across the frame. This port
keeps the same producer/consumer pipeline: main produces the DL, worker A
walks it and dispatches vertex batches to the IE64 for T&L, and the Voodoo
rasterises, all communicating through single-producer/single-consumer mailbox
rings in shared RAM (base 0x790000) with a doorbell MMIO write, and through
shared-RAM command and texture buffers rather than per-word MMIO. That
shared-RAM discipline is the analogue of the N64's shared RDRAM/DMEM access,
and it is why Voodoo submission is cheap (see Gate D and the MMIO dead-end in
the Gate J addendum).

One measured caveat on the split: worker A's triangle emit, not the IE64 T&L,
is the current bottleneck. The IE64 finishes a vertex batch before worker A
walks to the triangle that needs it, so T&L already overlaps for free; the
next material win is moving the emit path itself off the M68020 (see the Gate
J follow-up and standing conclusion). All three services are on by default
(`IE_TNL`, `IE_GFX_SVC`, `IE_AUDIO_SVC` = 1). Setting `IE_GFX_SVC=` or
`IE_AUDIO_SVC=` empty drops that stage to the local path on main; clearing all
three (`IE_TNL= IE_GFX_SVC= IE_AUDIO_SVC=`) puts every stage on main, the
single-core fallback and the parity reference for all the A/B measurements.

---

# Part 1 - Measurement methodology & standing rules

`make ie-fps` - measures wall time across a fixed swap span (350 to 470)
inside the title attract loop. Swaps map 1:1 to game frames via
display_and_vsync, so the span is the same scene content on every build
regardless of speed; numbers from different builds are comparable only under
identical polling, scene, engine, power and configuration (the script can
also overshoot swap 350 by a polling interval, and freeze cost, thermal drift
and attract-state drift all vary run to run).
Script: `ie/smoke/fps_measure.ies`. Warmup to swap 350 takes a few minutes
at current speeds.

Caveat: the script samples the status block via the debugger every 250 ms
(freeze/resume), a small constant overhead, fine for comparisons but not an
absolute frame-rate claim. **See Gate G below: freeze/resume polling
understates fps in proportion to how fast the game runs; every absolute
number from before Gate G carries a polling tax.** Prefer the poll-free
`race_fps_lite` / `svc_timing_probe` protocols for absolute numbers.

Standing rules for new measurements:
- For absolute numbers use the poll-free protocol (`race_fps_lite` /
  `svc_timing_probe`, see Gate G); the `make ie-fps` polling harness is
  retained only for A/B comparisons that need its historical swap window,
  and its freeze/resume polling understates faster builds. When using
  `ie-fps`, capture five runs before and after on the same idle amd64
  machine/session and compare medians only.
- Judge significance in relative terms at current speeds (percent change, or
  self-timed worker milliseconds); the old +/-0.1 fps rule was calibrated at
  ~1 fps and is too coarse for 30 fps-plus results.
- Keep the IE68 game build hard-float (`-m68881`); do not churn flags such
  as `-DNDEBUG` or `-fsingle-precision-constant` without fresh evidence.
- Do not enable LTO: segment-2 data relies on declaration order and the
  linker contract in `ie/game.ld`.
- `make smoke` intentionally excludes `ie-save-smoke`, because the save
  smoke deletes its scratch file-root before running.
- Thermal/power discipline (added 2026-07-12): battery discharge and
  sustained-load heat-soak both drift absolute numbers; on battery the
  self-timed worker translation counter (less sensitive to scheduling and
  debugger overhead than the probe's fps, though still bound by clock
  frequency and throttling) is the more trustworthy metric. On AC, interleave
  A/B/A/B to cancel any residual monotonic drift. Diagnostic probes live in
  `ie/smoke/` (`svc_timing_probe`, `svc_pan_probe`, `race_counters`,
  `jit_bail_probe`).

---

# Part 2 - Performance history

| build | title fps | delta vs baseline |
|---|---|---|
| Stage 3 as committed | 1.00 | baseline |
| + table trig (sins/coss via gSineTable) | 1.24 | +24% |
| + upstream AI/math port alone (without table trig) | 1.10 | +10% |
| + both combined | 1.25 | +25% |
| + prior audio/game review fixes | 1.40 | +40% |

Run-to-run noise is real: the combined build measured 1.15 once while builds
were running on the same machine and 1.25 on an idle machine. Keep the
machine idle during `make ie-fps` and treat +/-0.1 as noise.

Earlier ad-hoc number (1.97 fps "at title" pre-rebase) came from a different
swap window and different methodology; not comparable, do not mix with this
table.

## Table trig
`gSineTable` (0x1400 entries, `src/buffers/trig_tables.c`) was `#if 0`-ed out
and `sins`/`coss` fell back to `sinf`/`cosf`, costing hundreds of emulated
instructions per call (the build is hard-float `-m68881`, but every
instruction still pays emulation dispatch), on some of the hottest leaf
paths. Re-enabled the table; `sins`/`coss` are now static-inline lookups in
`src/racing/math_util.h`; `gCosineTable` is a pointer offset (+0x400) into
the same array so the N64 overlap trick stays standard-compliant. Host golden
test updated: table trig quantises the angle to >>4 resolution, so golden
values compare against the quantised angle.

## Upstream AI/math port
Hand-port of the game-logic fixes from upstream mk64-dc's `aica`/`backport`
branches (commits 4f033373 "so many fixes", 563ae0f3 "restore math without
breaking AI again", e897fc19 "more accurate trig, restore more old ai code"):
game AI restore (cpu_speed_control, path_calc, behaviour_utils), early-start
spin-out (`EARLY_START_SPINOUT_EFFECT`), collision refactor into
`compute_collision_triangle_*` helpers, math_util cleanups. 20 files,
+526/-852.

Port rules applied:
- All SH4 `shz_xmtrx_*`/`fsca` intrinsics rejected; kept our portable C
  implementations (16 conflict hunks in math_util.c all resolved to ours).
- `include <sincoss.h>` dropped; our `sincoss`/`scaled_sincoss` live in
  `math_util.h` (upstream centralised the same way, different header).
- Upstream debug leftovers stripped (`#include <stdio.h> // PROBE (remove
  me)` in player_controller.c and cpu_speed_control.inc.c).
- DC-only files (gfx_gldc, gfx_retro_dc, dcaudio, aica_synth, PVR backend)
  not ported; `src/audio/*` deltas deliberately excluded, since audio offload
  is its own workstream (see Part 3).

## Audio path status
The full game builds with `IE_AUDIO_VOICES`: the N64 sequence player still
owns note timing, envelopes, pitch, and note allocation, while each live note
is mapped to an engine SFX voice. The in-tree game presets are 0x68b0
(26800 Hz) with 16-28 max notes in `src/audio/audio_session_presets.c`; there
is no 13400 Hz / 8-note game preset to restore.

`IE_AUDIO_RATE` in `ie/ie_platform_audio.c` and `RATE_HZ` in
`ie/audio_main.c` are for the standalone mono ring-sink smoke image, not
linked by the full game voice path. Under `IE_AUDIO_VOICES`,
`ie_audio_buffered()` returns 0 and the full-game audio cadence is governed by
the voice poll window.

The engine reverb is global across all SFX voices, while MK64's original sends
are per-note; the voice path keeps engine reverb dry by default. Future audio
optimisation should use `IE_PERF_AUDIO_VOICE_WRITES` and live-voice counters
instead of changing presets speculatively.

---

# Part 3 - M68K coprocessor gfx/audio offload (implementation log)

Target steady state (parallel cores, N64-style CPU/RCP split): main M68K =
game logic + input + audio command production; M68K worker A = DL translation
+ Voodoo submit + frame pacing; M68K worker B = audio sequencing and voice
control through voice MMIO; IE64 worker = T&L vertex maths.
Measurements dated 2026-07-11 unless noted.

## Phase 0 - preflight

### P0.1 pack-layout guard
`ie/pack_ie68.py` rejects TOC blobs whose linear-load span intersects worker
RAM (M68K/x86/IE64) or the mailbox; case in `tests/test_pack_ie68.py`. PASS.

### P0.2 echo service bring-up
`svc` binary `build/ie/echo.ie68` (start stub `m68k_svc_start.S`, link script
`m68k_svc.ld` at 0x280000), driven two ways: `make
ie-m68k-echo-iescript-smoke` (IEScript-driven) PASS; `make ie-m68k-echo-smoke`
(guest M68K caller) PASS. Proves: BE reads of game RAM (0x10000000+) from the
worker, Voodoo MMIO writes from the worker, worker-to-worker IE64 ring-10
dispatch, response ring.

**Engine bugs found and fixed (IntuitionEngine):**
1. `createM68KWorker` set A7 but not the stack bounds, so every JSR/push
   bus-faulted and the worker halted (regression test
   `TestCoprocWorkerM68KStackPush`). Fix: `tuneStackBounds` + SSP/USP.
2. Worker CPUs never armed the JIT (`m68kJitEnabled` is only set by
   `NewM68KRunner`), so services ran interpreted, ~7x slower (measured 232 vs
   34 us/op on the P0.3 sweep). Fix in `createM68KWorker`.
3. M68K JIT inlines loads/stores with an unconditional byte-swap, breaking the
   CoprocMode swap-skip over the mailbox/shared window once the service loop
   got JIT-compiled (first request OK, every later one timed out). Fix:
   `initM68KJIT` marks pages 0x400000-0x7FFFFF as I/O for CoprocMode CPUs so
   they take the interpreter helpers (test `TestCoprocEndToEnd_M68K_JITHot`,
   needs vasm). Worker access to normal game RAM stays JIT-native.
4. (Phase 1) M68K JIT block compilation used package-global compile state
   (`m68kCurrentCS`); concurrent main+worker compiles panicked.
   `m68kCompileMu` now serialises `m68kCompileBlockWithMem`/`m68kCompileRegion`.

**Guest-side pitfalls encoded in the smokes:**
- IEScript `mem.read32/write32` are little-endian; guest RAM must be accessed
  byte-wise big-endian. `cpu.freeze()` (cheap counter) gates raw memory
  access; `dbg.freeze()` can deadlock against a busy-polling worker.
- With `-Ofast`, buffer stores sink past the volatile MMIO kick and result
  loads hoist above the completion wait. `ie_compiler_barrier()` (in
  `ie_mmio.h`) around the enqueue/wait fixes it; any future service caller
  (gfx, audio) must use it.

### P0.3 throughput gate - PASS
Kernel per op: 4 KiB sweep over guest RAM (read+XOR-write) + 1 Voodoo enable
write (+ optionally 1 IE64 ring-10 dispatch). 200-500 iterations, self-timed
via `ie_time_usec` on the executing CPU.

| Case | main CPU | M68K worker |
|---|---|---|
| idle, sweep+Voodoo+IE64 dispatch | 24-27 us/op | 29-38 us/op |
| idle, sweep only | 23-27 us/op | 24-26 us/op |
| attract race running, sweep only (`make ie-m68k-load-smoke`) | - | 28 us/op |

Worker under real game load is within ~15% of the idle main-CPU number,
inside the ~20% gate. Game held ~30 fps during the measurement. The load smoke
uses SWEEP_ONLY because ring 10 is single-producer and the game's T&L owns it
during play.

## P1.0 link-closure + state audit
TU set: gfx_pc, gfx_fast3d, gfx_cc, ie_gfx_voodoo, ie_gfx_window, ie_coproc.

External imports (nm, undefined minus set-defined):
- libc/libgcc: `memcpy memset memcmp sqrtf acosf __floatundidf`
- platform: `platform_fatal ie_time_usec ie_video_wait_vblank`
  (`ie_perf_add/inc/frame_end` only under `IE_PERF_COUNTERS=1`)
- game state: `segmented_to_virtual` (reimplement over the per-frame segment
  snapshot), `gIsMirrorMode` (frame-request flag)

Exports referenced by other TUs (main-side ABI to stub under `IE_GFX_SVC`):
- `gfx_init gfx_start_frame gfx_run gfx_end_frame` - src/main.c only
- `gfx_texture_cache_invalidate` - 8 TUs (render_player, render_objects,
  render_courses, skybox_and_splitscreen, actors, menu_items, libmio0,
  code_8006E9C0)
- `nuke_everything` - code_800029B0.c, racing/memory.c (gamestate reset, dispatched as
  OP_GFX_RESET_TEXTURES)
- `ie_gfx_window_api ie_gfx_voodoo_api ie_gfx_window_set_frame_hook
  ie_coproc_init` - ie_game_main.c

Size vs the 512 KiB worker window (text/data/bss):
- gfx_pc 30444/0/187576 (rgba32_buf 64K, buf_vbo 54K, gfx_texture_cache 48K,
  xform_buf 16K)
- gfx_fast3d 14304/0/4192 (gF3dRsp 4.2K), gfx_cc 1176/0/0
- ie_gfx_voodoo 19484/112/52068 (voodoo_cmd_buffer 32K, tex_slots 16.5K,
  shaders 2.3K, bake_cache 0.4K)
- ie_gfx_window 408/44/12, ie_coproc 2360/0/276
- **Total ~ 312 KiB + runtime shims + stack, which fits; no IE_GFX_SVC_RAM window
  needed.**

Worker-resident mutable state init order in the service entry:
gfx_init(&window_api, &voodoo_api) as ie_game_main does, initialising rdp/rsp state,
texture cache, color_combiner_pool, shaders/tex_slots/bake_cache,
voodoo_cmd_buffer, pacing statics in ie_gfx_window.

## P1.1 gfx_texture_cache_invalidate classification
Class (a) per-frame mutable during play, needs frame-parity double-buffering
before pipelining:
- `sKartTexture` slots in `D_802BFB80` (render_player.c:1908,2013); producer
  `mio0decode_noinval` rewrites per rotation frame. Backing arena 0x20000 =
  128 KiB, so the second copy costs 128 KiB.
- LR/WS jumbotron tiles `gLRTexture*`/`gWSTexture*` (render_courses.c 912-989,
  1196-1269; skybox_and_splitscreen.c 1629-1731 alias the SAME arenas);
  producer `copy_framebuffer2` per frame. 6 x 0x1000 per course, LR/WS
  mutually exclusive, so the second copy costs 24 KiB active (48 KiB if both).
Total double-buffer cost: 152 KiB active (176 KiB upper bound), which fits.

Class (b) transition-only, drain before the producing write:
menu_items.c (2804,3255,3794,4269,4376), libmio0.c:54 (decode-time),
nuke_everything callers (code_800029B0.c:179 setup_race,
racing/memory.c:339 load_course; already synchronous via OP_GFX_RESET).

Class (c) no mutation (cache-key churn only), no action:
render_courses.c:1838 (gRRWTexture648508), actors.c:739 (shell sprites),
render_objects.c:717,3715-3742 (static animation frames pointer-selected),
code_8006E9C0.c:580 (cloud sprites).

## Phase 1 - gfx service (worker A)
Implemented and building:
- `build/ie/gfxsvc.ie68` service (image ends 0x2CF7BC of 0x300000 at the
  build recorded here; distinct
  from the standalone graphics smoke image `build/ie/gfx.ie68`): six-TU second
  compile pass + `gfx_svc_main.c` ring-2 dispatch (INIT/FRAME/RESET),
  service-local `segmented_to_virtual` over the per-frame snapshot,
  `ie_coproc_attach()` for worker-side IE64 T&L dispatch.
- Client `ie/ie_gfx_svc_client.c` + seams in the six gfx_pc.c entry points
  (gfx_init/start_frame/run/end_frame + gfx_texture_cache_invalidate +
  nuke_everything), gated `IE_GFX_SVC`, local path intact as fallback;
  frame-flight drain, then enqueue, then audio pump; invalidate double-list; pack embed `svc/gfx.ie68`.
- Make gates `IE_GFX_SVC=1` / `IE_GFX_SVC_SYNC=1` + config stamp forcing
  gfx_pc.o rebuild on toggle.

Measured (sync mode, no pipelining, attract race, headless): local path
32.0 fps; IE_GFX_SVC sync 31.3 fps. Near-parity while fully synchronous, so the
pipelined win comes on top of an already-neutral base. Worker bring-up trace
markers at 0x9EF80 (op / phase / last ticket).

## Parity oracle (hardened) + pipelined bring-up
Oracle rework: the compositor hash was async to the guest swap, so some sample
points flapped. The engine records a deterministic per-swap hash: with
`IE_SWAP_HASH=1` the Voodoo swap worker hashes every presented frame right
after readback, keyed by swap sequence number; MMIO regs SEQ 0xF8358 / QUERY
0xF835C / VALUE 0xF8360, 64-deep ring (test
`TestVoodooSwapHash_PerSwapDeterministic`). Hash N is the guest's Nth swap in
any run. `gfx_svc_parity.ies` reads the regs via `mem.read32` under
`cpu.freeze()`.

Guest determinism window: local-vs-local runs are hash-identical for swaps
<= 800 and diverge somewhere in (800, 900] (attract-demo state drift); oracle
originally sampled swaps 150/300/450/600/700/800 (later narrowed to
150/300/450 as the game got faster and drift onset moved earlier; see Gate J).

P1.1 mitigations implemented:
- Class (a) kart arena: `KART_TEXTURE_ARENA` in src/buffers.h, a second
  128 KiB copy (`D_802BFB80_svcAlt`) selected by `gIeGfxKartParity`, which the
  client flips once per enqueued frame. render_player.c uses it everywhere;
  spawn_players.c one-shot decodes fill both copies. staff-ghost replay
  scratch and menu arraySize4 users keep the primary arena (contents must
  persist across frames). Correction (2026-07-12): the double buffer alone was
  not enough. Kart sprites are decoded only when the billboard angle moves past
  a threshold (the gate in init_render_player), so on a skipped frame the newly
  selected copy held a stale, different-angle sprite and the kart flickered
  between two facings (most visible under the pre-race camera pan). Fixed by a
  copy-forward: render_kart/render_ghost copy a skipped kart's slot from the
  other copy before reading it (`kart_arena_sync_slot`), keeping the selected
  copy current without re-decoding or an extra worker drain. The parity oracle
  did not catch this (it hashes swap output, not the intermediate arena).
- Class (a) jumbotron: not double-buffered: the consumers are baked course
  display lists (`gsDPSetTextureImage(..., gLRTexture...)`), and on IE
  `copy_framebuffer2` fills constant black (no LFB readback), so the write is
  idempotent and torn reads are impossible. Documented at the fill; needs a
  drain/double-buffer if readback ever lands.
- Class (b): `ie_gfx_svc_drain_for_write()` (client, no-op when idle or
  inactive) called from `mio0decode` and `tkmk00decode` before they overwrite
  pixels an in-flight frame may still reference.

Gate results (attract race, headless):
- Parity `make ie-gfx-svc-parity IE_GFX_SVC=1`: PASS both sync and PIPELINED
  (no IE_GFX_SVC_SYNC): all hashes identical to the local path.
- fps: local 32.0, sync 31.3, pipelined 32.54 (`ie-race-fps-lite`).
- Worker per-frame time (trace slot 3, includes the vblank wait inside
  gfx_end_frame): 11-22 ms typical, ~49 ms spikes; frames counter slot 4.
  Published at 0x9EF80+12/+16 alongside op/phase/ticket.
- Make: ie-game / ie-race-fps(-lite) depend on `$(IE_GFX_SVC_BIN)` so gated
  runs can't pick up a stale service image (this had bitten us: the first
  pipelined fps run measured 31.15 against an old gfxsvc.ie68).

## P2.0 audio seam audit - proceed, with conditions
The EU audio command queue survives intact in the port: game code calls
external.c directly, but every entry point defers through either the
sSoundRequests ring (play_sound*) or packed 8-byte EuAudioCmd entries in
sAudioCmd (port_eu.c:41, service build declares the pointer at :28), flushed
once per frame by func_800CBC24 (port_eu.c:270) as an OSMesg of
(producer|consumer<<8) indices. The pump (game_audio_pump_voices,
port_eu.c:90) drains the slice, runs process_sequences x updatesPerFrame and
the voice MMIO update. The N64 crossed a thread boundary at exactly this point.

- Command-block ABI: cmd_start/cmd_end u8 indices + the 8-byte EuAudioCmd
  slice (share the ring SPSC or copy <=2 KiB) + pump_ticks (catch-up count) + a
  time seed for gAudioRandom.
- Snapshot list: empty per frame. Worker needs only six boot-time pointers
  (five audio ROM segment buffers + gAudioHeap) latched at init.
- Feedback (audio-to-game reads, staleness-tolerant): gSequencePlayers
  enabled/tempo, channels[] pointer + soundScriptIO derefs (the one real
  hazard, transition-only, already guarded by IS_SEQUENCE_CHANNEL_VALID),
  gAudioRandom, gAudioResetStatus (boot).
- Shared heaps: gAudioHeap + pools mutated only audio-side (boot +
  transition-time sequence loads inside the pump). No per-frame game mutation.
  SFX voice MMIO written exclusively by ie_platform_audio.c.

Conditions before implementation:
1. Replace the AosSendMesg mailbox (ultra_reimpl queue mutates validCount from
   both sides, not cross-CPU safe); carry the (start,end) indices in the
   OP_AUDIO_PUMP request instead.
2. Ring-slot ownership: main fully writes a slot before advancing the producer
   index; add a high-water overflow guard (0x100 ring has none); handle
   func_800CBCB0's op=0 write-back (drop it worker-side or accept worker writes
   into the ring).
3. Channel-pointer feedback race at sequence-load transitions: gate the four
   channels[]-dereferencing readers on a worker-published load-in-progress
   flag, or feed soundScriptIO back via a per-pump block.
Boot: init audio entirely on worker B, main spins on a ready flag
(setup_audio_data's gAudioResetStatus drain moves with it).

## P2 implementation - audio service on worker B (2026-07-12)
Gates: game `IE_AUDIO_SVC=1` (client seams + `build/ie/audiosvc.ie68`), worker
pass `-DIE_AUDIO_SERVICE`. Composes with IE_GFX_SVC; local pump remains the
fallback on any COSTART/INIT failure.

Engine (generic multi-instance workers):
- `COPROC_INSTANCE` MMIO reg (0xF238C): instance selector for
  START/START_MEM/STOP/ENQUEUE + monitor regs. Worker table now (type,
  instance); M68K allows 2 instances, everything else 1. Worker-state bit 7 =
  M68K#1 (later retired; per-instance liveness moved to `COPROC_INSTANCE_STATE`,
  see the mailbox-ABI migration note). Monitor label `coproc:M68K#1`. TDD in
  coprocessor_multi_instance_test.go (9 tests), refman
  sdk/docs/Coprocessor.md updated.
- Second M68K window `WORKER_M68K2` 0x420000-0x49FFFF. It falls inside the
  CoprocMode byte-swap skip range (0x400000-0x7FFFFF), so it is carved OUT of
  isCoprocSharedAddr and out of the JIT I/O page marking: normal BE worker RAM,
  JIT fast path intact.
- **Mailbox layout bug found during bring-up**: a ring's content is
  RESPONSES_OFFSET + 16*16 = 0x308 bytes, 8 more than the then-current
  RING_STRIDE 0x300, so slot 15's response overwrote the NEXT ring's header.
  Harmless for rings 0-5 only by accident; the hot IE64 T&L ring (5) zeroed
  ring 6's head/tail/cap continuously. The stopgap parked ring 6 at 0x791300
  (clear of ring 5's overflow). Superseded by mailbox revision 1, which widens
  RING_STRIDE to 0x400 so no ring's content overflows its slot and every ring
  sits at the uniform `base + index x 0x400` (see the mailbox-ABI migration
  note). Documented in coprocessor_constants.go + refman.
- m68k_svc_start.S: stack top now `-DIE_SVC_STACK_TOP` parametrised (audiosvc
  links 0x0049ff00; default stays 0x002fff00). First symptom of the old
  hardcode: worker B booted with worker A's stack and bus-errored on the first
  JSR.

Game:
- Seam per the P2.0 audit. Main keeps the producer half (sound requests feeding a
  packed EuAudioCmd ring); worker B owns audio_init + boot reset drain,
  sequence loads, process_sequences, and the voice MMIO updates.
- Audit conditions: (1) OP_AUDIO_PUMP request carries (cmd_start, cmd_end)
  indices, and the OSMesg mailbox is compiled out in svc mode; (2) producer slot
  writes are fenced before the index bump (port_eu.c), client keeps a 0xC0
  high-water guard on the 256-slot ring; worker-side op=0 write-back kept
  (slots [start,end) are worker-owned until the pump response); (3) worker
  publishes a load-in-progress flag + its &gSequencePlayers /
  &gSequenceChannelNone through a feedback block at 0x9EFC0; the main-side
  readers redirect via ie/ie_audio_svc_redirect.h and IS_SEQUENCE_CHANNEL_VALID
  fails closed while a load is in flight (the two previously-unguarded deref
  sites in external.c gained the check).
- New files: ie/coproc/audio_svc_proto.h (ops 'AINI'/'APMP', feedback block
  layout), ie/coproc/audio_svc_main.c (ring-5 dispatch),
  ie/coproc/audio_svc_stubs.c (link closure: producer-half game globals,
  identity segmented_to_virtual, byte n64_memcpy, minimal libultra subset;
  linking ultra_reimpl.c whole costs ~33 KiB .bss the window can't spare),
  ie/coproc/m68k_svc2.ld (0x420000), ie/ie_audio_svc_client.c (COSTART instance
  1, INIT sync, one drain+enqueue pump per ie_audio_poll window).
- main.c: setup_audio_data branches to ie_audio_svc_init() after _AudioInit
  (host backend + audio MMIO enable stay on main); sound_init (producer state)
  still runs on main. game_audio_pump routes to ie_audio_svc_pump when active.
- Pack: svc/audio.ie68 embedded (8th pack arg); pack guard + test extended with
  the 0x420000 window.
- Image: 147076-byte file (143.6 KiB), bss to 0x49D344 at the build recorded
  here, with ~11 KiB headroom under the 512 KiB window (gAudioHeap 291 KiB
  dominates).

Bring-up verification (headless):
- ie/smoke/audio_svc_probe.ies PASS: 'ASVC' feedback magic after worker INIT
  (audio_init + reset drain on worker), pump counter advances at frame cadence
  (~120/2s).
- Sequencer liveness: player 0 enables when the title music load command
  crosses the seam; producer index advances; pump 0.4-0.9ms with music, ~0.1ms
  empty.
- ie-race-fps-lite (span shortened from 25 s to 20 s: the game now outruns the
  attract race window): gfx+audio 33.8/35.5/35.8, gfx-only 33.5/34.9/35.5 (same
  evening, same engine). Audio offload is between neutral and +0.5 fps
  at the attract race; its win is main-CPU headroom (~25% of the old frame was
  audio in the P2 PC samples).

Exit gates: audio A/B by ear vs local path (music, sfx, pitch, timing) passed
on 2026-07-12 (signed off, audio judged perfect); this is why `IE_AUDIO_SVC`
is now default-on. Visual pass still recommended. Engine coproc suite green
(118 + 9 new); pre-existing Z80/IE32 worker races on HEAD unchanged.

Review fixes (2026-07-12): client overflow guard measured backlog after
advancing g_last_sent (never fired), now measured against the previous
baseline before the update; engine calibrateDispatchOverhead clobbered
guest-visible shadow registers (including COPROC_INSTANCE), now saved/restored
under the lock (TestCalibration_RestoresGuestShadowRegisters). The generated
svc_pc_sample/svc_timing_probe scripts were only produced by the game.bin link
recipe, so incremental builds ran missing/stale scripts; a standalone pattern
rule (`$(IE_BUILD_DIR)/%.ies` from `ie/smoke/%.ies.in`) now instantiates them,
and game.bin/game.map are a grouped target (`&:`) so a map deletion forces
regeneration.

---

# Part 4 - Decision gates A-J (perf ranking + levers)

## Gate A - post-WP2 counters
`make IE_PERF_COUNTERS=1 IE_BUILD_DIR=build/ie-counters ie-counter-profile-run`.
Engine `27ca532c`. Window: early title/menu, swaps 4 to 63, 60 samples. This is a
counter-ranking run, not an fps run.

| counter | total | peak/frame | avg/frame |
|---|---:|---:|---:|
| tris | 39,164 | 675 | 652.73 |
| draw_calls | 195 | 4 | 3.25 |
| mmio_writes | 1,340,334 | 24,349 | 22,338.90 |
| audio_voice_writes | 8,256 | 1,440 | 137.60 |

(texrects, tex_imports, tex_stream_bytes, clipped_tris, vtx_mtx_reuse, all
memcpy buckets = 0.)

Ranking: (1) backend triangle/MMIO work dominates: 39,164 tris become 1.34M MMIO
writes; the engine command FIFO is the real target. (2) Audio voice writes
material, re-rank at Gate B with a race scene. (3) transform-cache work not
justified (`vtx_mtx_reuse` = 0). (4) texture residency/bake not justified
(imports/stream = 0). (5) rectangle path skipped (`texrects` = 0).

## Gate B - post-Wave-2 counters
Same window, after WP5 reciprocal/projection hoists, WP4
residency/shadow/bake/boot-status cleanup, WP6 matrix flush-barrier removal.
tris/draw/mmio essentially unchanged (mmio 1,339,981). Ranking: (1) residual
render cost still overwhelmingly MMIO dispatch, i.e. the engine command FIFO. (2) WP7
audio voice shadowing next (8,256 writes). (3) blitter skipped (memcpy = 0).
(4) collision/transcendental need a race profile. (5) transform-cache skipped.

## Gate C - post-WP7 counters
Swaps 5 to 63, 59 samples, after WP7 audio voice shadowing. `audio_voice_writes`
dropped from 8,256 to **35** (Gate B to C), mmio ~1.31M. WP7 mechanically confirmed;
render cost still MMIO dispatch in the engine command path.

## Gate D - post-item-23 Voodoo command stream
Engine worktree with the Voodoo command-stream ABI (`VOODOO_CMD_PTR`,
`VOODOO_CMD_COUNT`, `VOODOO_CMD_SUBMIT`). Swaps 6 to 63, 58 samples, after
batching guest Voodoo register writes into a guest-RAM command stream.
**`mmio_writes` dropped from 1,308,945 to 1,046 (avg 22,185.51 to 18.03/frame).** The
focused counter smoke reports `mmio 3` for one rendered frame (pointer, count,
submit). Triangle/draw shape unchanged, so the command stream removes guest MMIO
dispatch without changing the rendered workload. *(2026-07-12 follow-up: a
cmd-stream on-vs-off A/B on worker-A translation measured identical: the
Voodoo register addresses are on the JIT's native-inline path, so this MMIO was
never actually the bottleneck; the command stream is architecturally right but the
residual ~245 MMIO/frame at the race costs nothing.)*

## Gate E - post-item-22 Voodoo bulk texture upload
Engine with the bulk texture-upload ABI (`VOODOO_TEX_SRC_PTR`,
`VOODOO_TEX_SRC_BYTES`). Texture residency now emits bulk source
pointer/byte-count/upload writes instead of one MMIO write per texel word (host
capture asserts zero texmem MMIO for uploads, bake-content checks preserved via
a non-MMIO mirror). The engine bulk path copies BE u32 texel words from guest
RAM and converts to the backend RGBA byte stream. The title/menu profile has
tex_imports/stream = 0, so it is not a useful speed gate; a texture-heavy/race
profile is still needed for the end-to-end win.

## Gate F - host-side profile, JIT dispatch purge, frame pacing
Guest counters cannot see host cost, so this gate used host profiling
(`perf record -F 499 -g -e cycles:u`, 120 s, boot to title). Pre-gate host
profile (~1.7 of 16 cores busy): ~32-41% `VideoCompositor.blendFrameScaled`
(software compositing every scheduler tick, nobody consumes headless frames);
~15% software Voodoo raster (all fps numbers here are software-raster:
headless/novulkan builds; only default-tag builds carry Vulkan);
the guest 68k thread spent ~2/3 of cycles on per-dispatch bookkeeping
(`os.Getenv` per block dispatch, a CRC32 re-hash of guest block bytes per
execution, always-on MMIO stats map lookups, Go-map block dispatch).

Engine changes (`3dfx` branch): cache the trace env var at startup; gate MMIO
stats lookups behind `IE_MMIO_STATS`; new `IE_M68K_JIT_DISPATCH_HASH=0` opt-out
skips the per-dispatch block re-hash (SMC stays guarded by native code-page
write detection + the invalidation queue; default on so AROS-style
self-modifying guests are unaffected). This repo sets the opt-out via
`IE_RUN_ENV ?= IE_M68K_JIT_DISPATCH_HASH=0`.

Game change: `wm_swap_buffers_end` frame pacing was double-penalising slow
frames: it slept a full 16.7 ms period after every swap even when behind
60 Hz, then aligned to the next VBlank edge, quantising to whole VBlank periods
(pre-fix builds sat in the 60/7 = 8.57 fps bucket). Pacing sleep + VBlank
alignment now apply only when the frame finished inside its 60 Hz slot, and the
deadline re-anchors to actual post-VBlank time so a short next frame can't take
the late branch and burst past 60 Hz (review catch: the burst also inflated
measured fps by ~2).

Measurements (`make ie-fps`, idle, 5 interleaved runs, medians, spreads
<= 0.36):

| config (all with pacing fix) | median fps |
|---|---:|
| engine without purge | 14.49 |
| + Getenv cache and MMIO stats gating | 15.28 |
| + dispatch re-hash opt-out | 15.75 |

Ladder: 1.40 to ~8.5 (command stream + wait, quantised) to 14.49
(pacing fix) to 15.75 (+ engine purge). Same-workload host cycles -~24%.
Ranking: (1) engine composite-on-demand in headless. (2) engine JIT dispatch
Go map replaced by a direct-mapped array. (3) structural host-side geometry/raster service
(the DC/PVR division of labour). (4) re-profile a race scene before WP8/WP9.

## Gate G - poll-free measurement, race scene, present path
**Measurement correction (invalidates earlier absolute numbers):** every fps
script before this gate sampled the status block via debugger freeze/resume.
Each freeze stalls the pipeline hundreds of ms, so polling understates fps in
proportion to actual speed, harmless at 1.4 fps, a ~75% error by this gate.
Same build/scene: attract race gave 8.4 fps (4 Hz polling), ~6 fps windowed
(1 Hz), and **31.9 fps headless / 8.5 fps windowed poll-free**. Relative A/B
under identical polling stays valid; earlier absolute numbers carry the tax.

New scripts: `race_fps.ies` (4 Hz), `race_fps_lite.ies` (poll-free, two samples
bracketing an untouched span; `make ie-race-fps-lite`). Prefer the lite variant.

Poll-free race results (2 runs/config):

| config | race fps |
|---|---:|
| headless, pre-gate engine | 31.79 / 31.91 |
| headless, compositor-gate engine | 30.27 / 31.99 |
| windowed Vulkan, pre-gate engine | 8.55 / 8.35 |
| windowed Vulkan, compositor-gate engine | 9.50 / 9.57 |

Decisions: (1) the guest is no longer the playability bottleneck: headless
~32 fps = native N64 pace. (2) the gap is the windowed present path (~32
headless falling to ~9 windowed: Vulkan fence wait, then staging readback, then compositor copy,
then display upload, serialised against the swap). (3) compositor
frame-generation gate confirmed windowed: +13%. (4) swap pipelining >1 job was
tried and reverted: the backend mutex held across raster/fence stalls
forwarded writes; prerequisite: backend must not hold the lock across the full
frame. (5) interleave configs, compare medians.

## Gate H - windowed present path fixed: race at native pace
Swap-stage buckets (`IE_PERF_ACCT=1`, via race_fps_lite `sys.perf_report`)
attributed the windowed loss: Vulkan render submit ~21 ms/frame. Two engine
defects, both texture-path: (1) GPU snapshot-texture cache keyed by snapshot
pointer: the guest streams every texture through one upload window, giving a fresh
pointer per switch, so every draw group re-uploaded its texture each frame
(fixed by content-keyed caching); (2) SetTextureData uploaded eagerly with a
full vkQueueWaitIdle on the guest's forwarded-write path, one synchronous GPU
round trip per texture switch, dozens/frame (fixed by deferring the live-slot
upload to the flush, at most once/frame on the swap worker).

| build | before | after |
|---|---:|---:|
| windowed Vulkan | 7.4-9.5 | **31.0-32.4** |
| headless software | 30.3-32.0 | unchanged |

The demo race now runs at native N64 pace (~30 fps) both headless and on
screen. Engine commits 934d1513 (compositor gate), b3da54a2 (stage timing),
a630dabd (texture path). Keep race_fps_lite + IE_PERF_ACCT as the standard race
measurement. Remaining known engine items (not gating at this pace):
debugger freeze/resume stalls the windowed pipeline; backend mutex held across
raster/fence blocks forwarded writes (prereq for swap depth >1);
TestVoodooMegaDemoBasicRunAOTSmoke fails at engine HEAD independently.

## Gate I - backend lock granularity, 60 fps scoping
Engine commit 9e84470f: both Voodoo backends render flushes from per-flush
state snapshots (software: framebuffer lock only; Vulkan: render lock + brief
state-mutex snapshot at entry), guest register writes never wait on an
in-flight raster/fence, swap pipeline allows two jobs in flight.
Race-detector clean. Poll-free race fps: headless 30.6-33.5 (parity), windowed
31.1-31.5 vs 30.4 (marginal). The expected 40-45 did not materialise for a
measured reason: after the Gate H texture fixes, render (~5-9 ms) already hides
entirely inside the guest sim (~30 ms), so the guest never waited at swap and
lock contention had nothing left to cost. Race frame time is now pure guest
sim.

**Consequence for 60 fps:** no further engine present-path work can raise race
fps materially. The guest frame must drop below 16.7 ms of emulated work, so
move display-list translation (gfx_pc/fast3d walk, vertex transforms)
host-side over the command-stream ABI (the display-list offload package). The
lock granularity here is its concurrency prerequisite.

## Gate J - coprocessor offload package measured (Phase 3)
Config: IE_GFX_SVC=1 + IE_AUDIO_SVC=1 (gfx on worker A, audio on worker B, T&L
on IE64), pipelined, attract-race steady state, headless. 2026-07-12 figures on
battery (~2.4 GHz): absolute fps understates AC; ratios/A-B within a session
valid.

Worker self-timed (svc_timing_probe.ies, cumulative counters read twice around
an untouched span; the template's SPAN_MS is now 8 s, shortened from the 20 s
used for the dated A/B tables below as the game outran the attract window.
A debugger freeze costs ~0.7 s guest progress, so
per-frame polling corrupts what it measures; the old per-sample probe
"measured" 11.7 fps / 41 ms on a build that free-runs at 30+):

| metric | value |
|---|---:|
| fps over span | 29.7-31.0 (battery; 33.5-35.8 the evening before) |
| worker A translation+submit | avg 30.5 ms |
| worker A frame incl swap wait | avg 31.9 ms |
| worker B audio pump | avg 1.1-1.2 ms |

**Worker A is the pace-setter** (translation+submit ~96% of its frame; vblank
wait ~1.3 ms). Main-CPU sampling shows game logic spread thin (no bucket above
2.5% once the freeze-recovery drain-spin artefact is discounted; the 83%
"ie_audio_svc_pump" bucket is main spinning in svc_drain while frozen workers
thaw, an artefact, proven by the fps A/B neutrality of the audio gate).

Worker A busy profile (svc_pc_sample 3-CPU run, worker buckets renormalised to
busy time, statics via per-object nm):

| share | function |
|---:|---|
| 33% | ie_gfx_voodoo vd_draw_triangles (per-vertex emit + combiner eval) |
| 18% | ie_coproc tnl dispatch/drain (IE64 wait; async overlap ~zero) |
| 14% | gfx_pc append_rgba_input (texture decode) |
| ~20% | DL walk (gfx_run_dl, sp_tri1, emit_vertex, clip) |
| 5% | gfx_sp_matrix/matrix_mul |

Tuning this gate: vd_draw_triangles bulk-reserves its command pairs
(vd_cmd_reserve/vd_cmd_commit; 34 pairs/tri) instead of the per-write
streamable/capacity checks in vd_mmio_write32; translation 32.1 to 30.5 ms
(~5%, borderline vs battery noise; parity-verified, kept). Worker B publishes
cumulative pump usec (feedback +0x1C), worker A cumulative translation/frame
usec (trace slots 6/7/8).

Parity oracle recalibrated to swaps 150/300/450: local-vs-service hashes
diverge from swap ~560 (interleaved same/diff pattern, parity_onset.ies):
attract-demo state drift whose onset moves earlier as the game speeds up
(local-vs-local stable to 800 on 07-11, diverges at 700 now). Not a rendering
fault; verified pre-existing at HEAD with the emit change reverted, audio
on/off.

Doubling regression: IE_VOODOO_DUP_TRACE=1 over a full boot-to-race run (39
one-second windows, 28-44 publishes/s at the race): 0 identical and 0
centre-identical publishes in every window. The GetFrame monotonic fix holds.

60 fps scoping: worker A must drop from ~30 to <16.7 ms. Removing the emit (33%) +
texture-decode (14%) shares projects ~30 to ~21 ms, still short; (2) T&L
overlap or a faster worker core is needed.

### Gate J follow-up - T&L overlap rewrite built, measured, REJECTED
Double-buffered vertex banks + deferred triangle emission (worker A emits batch
N's triangles while the IE64 worker transforms batch N+1), implemented in full,
same-battery A/B:

| build | fps over 20 s | worker A translation+submit |
|---|---:|---:|
| baseline | 30.2 | 31.6 ms |
| T&L overlap | 27.2 | 33.9 ms |

**Overlap regresses (~10% fps, +2.3 ms).** The premise was that worker A stalls
at the drain waiting for IE64; it does not: the IE64 worker finishes the batch
before worker A walks to the first triangle that needs it, so the drain returns
instantly. The rewrite adds a per-swap full vertex-bank memcpy plus queue
bookkeeping, all overhead against a drain that was already free. Parity held
(correct, just slower). Reverted. The 18% "tnl dispatch/drain" bucket is
therefore dispatch cost (writing the ~152-byte request header + ring descriptor
per batch, LE byte stores), not wait, so cut it with a cheaper request encoding or
fewer/larger batches, not overlap. 60 fps needs a faster worker core (move the
emit path to the IE64 worker).

### Gate J lever (1) - combiner-input hoist: ACCEPTED (2026-07-12, AC)
`emit_vertex` (src/gfx/gfx_pc.c) re-ran the per-input `switch
(comb->shader_input_mapping[0][j])` for every emitted vertex, re-resolving
constant inputs (PRIM/ENV `byte_to_unit`, default white) 3-6x per gfx_sp_tri1.
The classification is constant per material; only SHADE and LOD vary per vertex.
Hoisted: `build_emit_inputs()` resolves a 4-entry `struct EmitInput` descriptor
once per gfx_sp_tri1; emit_vertex branches on the descriptor kind. Pure
arithmetic-identical refactor. Parity clean.

Same-power A/B (AC, svc-timing-probe, 20 s span):

| build | worker A translation+submit | fps |
|---|---:|---:|
| baseline | 26.6 ms | 34.2 |
| hoist (run 1) | 25.7 ms | 34.9 |
| hoist (run 2) | 24.0 ms | 36.1 |

~3-10% off worker A translation. Modest but risk-free; kept.

### Gate J lever (2) - combiner hoist in the Voodoo backend: ACCEPTED (2026-07-12)
Same loop-invariant hoist on the bigger hotspot: `vd_draw_triangles`
(ie/ie_gfx_voodoo.c, 33% of worker A) re-ran the `cc->c[0][ti]`/`cc->c[1][ti]`
SHADER_INPUT/TEXEL range-test cascades for every vertex, eight branch chains
per vertex, though the term classification depends only on
`current_shader->cc`, constant for the whole draw. Hoisted: classify the 4
colour + 4 alpha terms once per draw into `cc_term_kind[]`/`cc_term_off[]`
(0 zero / 1 input at attr offset / 2 texel=1.0); the inner loop indexes the
descriptor. Fetched values and `(a-b)*c+d` unchanged.

Bit-exact: swap-hash 150/300/450 identical to HEAD (e4627cfa/8890683d/597eb5d7),
parity oracle clean.

Same-power A/B (battery, 20 s span, hoist ran second so battery droop
penalises it):

| build | worker A translation+submit | fps |
|---|---:|---:|
| baseline (HEAD) | 34.97 ms | 27.30 |
| voodoo hoist | 29.57 ms | 29.97 |

**-15.4% worker A translation, +9.8% fps, the largest single Gate J lever, on the
largest share.** Self-timed translation delta (-5.4 ms/frame) is the clean
metric; fps corroborates.

### Gate J lever (3) - content-keyed texture-decode cache: REJECTED (2026-07-12)
Hypothesis: a dirty invalidate that re-decodes byte-identical source re-uploads
an identical RGBA32; fingerprint the decoded-from source (FNV-1a over
loaded_texture.addr[0..size_bytes] + TLUT for CI) and skip decode+upload on a
hash match; adaptive miss-streak (>=8) disables hashing for per-frame-unique
nodes (jumbotrons). P1.1 invalidate-caller classification confirmed kart
sKartTexture (content repeats while rotation stable, so predicted high hit),
object/snowman/shell (static ROM, spurious invalidate, so always-hit),
jumbotrons (genuine per-frame-unique, so miss), menu/mio0/cloud (transition-only).
Implemented, bit-exact (no false hits). **Measured at the attract race: hit
rate 2.8% (87 skips / 3065 decodes over 699 frames), translation
indistinguishable from hoist-only.** The attract demo's decode load is genuine new-content decode;
the kart-rotation-stability win does not appear under AI karts that turn
constantly. Rejected on the same evidence discipline as the T&L overlap. A
stable-camera gameplay
measurement could revisit but is not headless-measurable today.

### Gate J addendum - later measured dead-ends (2026-07-12, AC, current engine build)
- **Matrix-load reuse** (skip identical `gfx_sp_matrix` LOAD via an
  `mp_consistent` guard): correct + host-tested bit-exact, but AC interleaved
  A/B = -0.35% (noise). Reuse rate during active racing is low. Kept as a
  risk-free no-op, not a win. Committed with the pan probe.
- **Intro-pan cliff**: freeze-free `svc_pan_probe` bracketing the pan (first
  RACING swaps) shows **18.6 fps, worker-A translation 47.4 ms/frame, idle-wait
  17 us**, 100% gfx-translation-bound (~43% heavier DL than the 33 ms race,
  the fly-by sees the whole course + 8 karts), not audio and not main-side. The
  earlier "0.1 fps pan" was an `intro_pc_sample` freeze artefact (15 ms
  freeze/sample).
- **MMIO to direct-RAM idea**: already the architecture (RAM command buffer +
  3-write doorbell; `TEX_STORE` slot RAM). Race counters: 12,421 register
  writes/frame already in RAM, only 245 real MMIO/frame. cmd-stream on versus
  off gives identical worker-A translation, so Voodoo register MMIO is
  native-inline, not slow. No lever.
- **`vd_upload_texture` fuse** (decode+pack in one pass): worker-A PC sample
  puts vd_upload at 0.3%. Not worth it.
- **JIT-friendliness of guest C**: measured JIT fallback = **0.006% of
  instructions** at the race (top fallbacks are Line-F FPU precision-qualified
  `fcmp`, i.e. a rounding-precision difference). No
  transcendentals/soft-float/int-div in the hot
  path. Vec3f/Mat4 are array typedefs (already by-reference). The scalar
  per-instruction JIT won't emit SSE/AVX/FMA from guest code: SIMD belongs in
  the JIT or an x86-native service, not guest C.

**Standing conclusion:** the low-cost game-C levers are exhausted. Worker A
`vd_draw_triangles` emit (33%) is the dominant remaining cost; the only
material remaining lever is engine-side: a native host geometry/emission
kernel over the command-stream ABI (SSE/AVX/AVX2, no FMA under the bit-exact
regime), gated on rebuilding a deterministic parity oracle (the swap-hash
oracle is currently unreliable on the current engine build: HEAD
local-vs-service hashes are identical at swaps 150/300/450 but diverge from
swap ~560). This is the plan's deferred "geometry front-end" project, not a
patch.

## Mailbox-ABI migration - layout revision 1 (2026-07-13)

The engine reworked the coprocessor mailbox ABI. This port was resynced to it;
without the changes below every service was rejected at START with
`COPROC_ERR_STALE_WORKER` (error 9). Guest mirror of the constants lives in
`ie/coproc/coproc_layout.h`.

What changed on the engine side:
- **Uniform 0x400 ring stride.** RING_STRIDE widens from 0x300 to 0x400, so a
  ring's 0x308-byte content no longer overflows into the next ring's header.
  This retires the ring-6-at-0x791300 stopgap: every ring now sits at the plain
  `base + index x 0x400`.
- **Ring index = cpuTypeToIndex x 2 + instance.** With cpuTypeToIndex(M68K)=2
  and cpuTypeToIndex(IE64)=5, the ring assignments moved: worker A (gfx, M68K
  instance 0) ring 2 -> 4 (0x791000); worker B (audio, M68K instance 1) ring
  6 -> 5 (0x791400); IE64 T&L instance 0 ring 5 -> 10 (0x792800).
- **START-time version gate.** The host publishes `COPROC_LAYOUT_VERSION` (1) at
  `ring_base + 0x03` and clears the ack at `ring_base + 0x04`. A conforming
  worker must echo the version into `+0x04` within the START handshake window
  (100 ms) or START tears the worker down and returns `COPROC_ERR_STALE_WORKER`.
- **Retired WORKER_STATE bit-7 overload.** Per-instance liveness now reads from
  `COPROC_INSTANCE_STATE` (0xF25B8), bit `cpuType x 2 + instance`, a single
  atomic read that needs no CPU_TYPE/INSTANCE selection. `COPROC_WORKER_STATE`
  keeps only the per-type "any instance online" bits (bit = cpuType).

What changed on this port's side:
- `ie/coproc/coproc_layout.h`: stride, mailbox end (0x793000), ring indices,
  and the new ack offset + layout version.
- Each service echoes the version at startup before its poll loop:
  `gfx_svc_main.c`, `audio_svc_main.c`, `echo_svc_main.c` write
  `IE_COPROC_LAYOUT_VERSION` to `ring_base + 0x04`; `tnl_service_ie64.asm`
  stores it via `store.b` at entry (and its RING_INDEX/RING_STRIDE track the
  new layout).
- `ie/coproc/ie_coproc.c`: the main-CPU IE64 direct-ring path moved from
  0x790F00 to 0x792800.
- `ie/ie_audio_svc_client.c`: worker-liveness check moved off the retired
  WORKER_STATE bit 7 to `COPROC_INSTANCE_STATE` bit 9 (M68K instance 1).
- `ie/pack_ie68.py`: the reserved mailbox span end moved 0x791800 -> 0x793000
  so packed blobs cannot land in the rings the wider stride added; regression
  case in `tests/test_pack_ie68.py`.

Verified: `make ie-race-fps-lite` against the rebuilt engine reports all three
services online ("TnL coprocessor online", "audio service online (M68K worker
B)", "gfx service online (M68K worker)") with no STALE_WORKER, ~33 fps.

## Acknowledgements (historical)

This project builds on and carries forward work from earlier efforts:

- The MK64 decompilation team and the N64 Decompilation Discord
  (#mk64-decomp), MegaMech, and the Spaghetti Kart project.
- The earlier Dreamcast port and its contributors, spanning shared-code fixes,
  backend and tooling work, and artwork: Falco Girgis, Paul Cercueil,
  Luke Benstead, @stiffpeaks, John Brooks, and jnmartin84.
