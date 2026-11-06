# Devlog

Milestones of DKB Recomp, newest first. The plan they follow is [bananza-plan.md](bananza-plan.md); open work is in
the [roadmap issue](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/10).

## 5 November 2026

- **The destructible terrain renders by default.** `native_gs` (#67) is now on unless `TOTK_NATIVE_GS=0`. On an RTX
  5060 Ti the rocks drew with textures in hand-played gameplay up to the end-of-tutorial cutscene, and philneitz saw
  the same on NVIDIA and on a Radeon 610M. The cutscene's device loss (#75) happens with the switch on or off, as does
  the Radeon's own device loss.
- **The destructible terrain renders with `TOTK_NATIVE_GS=1`** (#67 by philneitz, 0ee59b2, [#45](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/45)).
  The terrain's passthrough geometry stage now replays natively and fills the indirect counts that the reset from #48
  leaves at zero. In the mining tunnel the walls and floor render, where they are black with the switch off. It is
  translator patch 0018 and stays opt-in. A 180 s tutorial run with the Vulkan validation layer had 0 faults and the
  same 10 validation errors as with it off.
- **Written ranges go through a hash table** (#74 by philneitz, a8d6afc). `note_written` searched its whole list on
  every call. In the Hilltop Layer with the geometry stage it took most of the replay thread.
- **The opening wall's cause is found** ([#45](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/45), by dwk001).
  The translator's decoder treats an unpredicated `CAL` as an unconditional branch, so the code after the call loses
  its predecessor. In a square root's slow path the result is then overwritten with `0 × inf = NaN`. The one-line fix
  brings the wall back here as well.
- **The opening wall is back** (#78 by dwk001, 8167596). Translator patch 0019 lets the code after an unpredicated
  `CAL` run, and the Ampere lowering now accepts `TXD` with an R G B mask, so three stages that failed to lower after
  the tutorial translate ([#76](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/76)). The wall's lighting is
  still darker than in Eglantine.
- **A passthrough geometry stage may read an input it does not forward** (#81 by dwk001, 5bce1ef). One cached stage
  failed reflection on every start because of that.
- **Shadow memory sync looks up pending GPU writes by address** (#82 by dwk001, 87ae189) instead of scanning the
  whole list for each copied range. Shadow storage is still off by default on Windows.
- **A device lost in the end-of-tutorial cutscene** ([#75](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/75))
  reproduces on two machines and two drivers, with and without the `CAL` fix. Skipping the cutscene avoids it.

## 4 November 2026

- **The NaN on DK's face is fixed** (#72 by zswxp59, 60e7616, [#70](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/70)).
  DK's skinning stage packs tangents as signed bytes with Ampere `F2I.S8`. The lowering turns that into a Maxwell
  F2I with an 8-bit destination, which the translator read as unsigned, so every negative component was stored as 0
  and some tangents became zero vectors. DK's vertex stage normalized them into NaN, and the bloom spread it into
  the headlamp halo and the black blot. Translator patch 0017 converts with the right sign and saturates to the byte
  range before converting. In the frame 7400 scene the NaN goes from every attempt to 0, without the FMZ experiments.
  The missing opening wall in #45 has another cause.
- **NVN's alpha test applies** (#68 by philneitz, 73aade7). Vulkan has no alpha test, so the pipeline gets a fragment
  variant that discards when output 0's alpha fails the compare. The skill tree menu no longer shows black blocks.
- **Mixed routes reach the output** (#66 by monsterlag, 9a31b48, [#63](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/63)).
  Submixes fed through a splitter were mixed after their destination, so their sound was lost: the banana jingle,
  the area title card, area music and more. They are now ordered as a graph. Two main menu sounds still play in the
  right channel only.

- **No more clip and cull validation errors** (5ffe6ad). The translator declares `CullDistance[1]` next to
  `ClipDistance[8]`, which together exceed the device's limit of 8, and the runtime drops the member when nothing
  uses it. The check read the instruction number of `OpExtInst` as an id, and `Fma` (50) matched the `gl_PerVertex`
  variable in 90 cached vertex shaders, so those kept it. A 150 s tutorial with the validation layer now reports no
  error, against 10 before.
- **Portable geometry replays with owned controls** (#55 and #56 by monsterlag, c053676,
  [#45](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/45)). The passthrough geometry stages replay under
  `TOTK_EXPERIMENTAL_GEOMETRY=1`, with cache entries checked against their source and program controls each draw
  owns. Their writes and the indirect reads that follow them run in one submission, with the barrier between them
  tested on the GPU by `geometry_ordering_probe`.
- **Geometry stages read their subgroup inputs** (#60 by monsterlag), and **`tools/extract_nup.py` creates its
  temporary files exclusively** (#59 by dwk001).
- **A crash after the Baboomtown loading screen** (e87cd32,
  [#10](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/10)). When a draw's fragment stage had no resources,
  its descriptor set 1 had no bindings, and set 0 was pushed with a byte count of 0. The driver then read the
  descriptors from whatever followed in the command recording. Set 0 is now pushed whole. The crash doesn't happen
  on our machine, so the fix waits for a retest.

- **Geometry stages replay behind a flag** (#51, #52, #53, #54, #57, #58 by monsterlag, 5ff620b,
  [#45](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/45)). With `TOTK_EXPERIMENTAL_GEOMETRY=1`, draws with
  an ordinary geometry stage replay through it, and `--probe-vkgeometry` checks a points-to-triangle stage and the
  indirect record it writes on the GPU. The translator accepts a portable passthrough subset when the request names
  which varyings pass through (patch 0016). Shader-cache contracts are read only from root fields. Ampere program
  capture no longer stops at an EXIT whose second predicate can fall through, a check Eglantine's copy has too. Two
  opt-in tools help with the terrain: `DKB_GEOMETRY_CONTEXT=1` logs the first geometry draws and their buffers, and
  `tools/geometry_body_footprint.py` reports which attributes a geometry stage reads and writes. The terrain is
  still black, because its passthrough stage is rejected by the replay until #55. With the flag off, the mining
  tunnel replays in 4.91 ms, as before. With it on, the replay takes about 0.95 ms more even though no geometry
  stage runs there.

## 3 November 2026

- **The terrain record reset costs half as much** (9b12091). The reset used a full barrier before each of its
  roughly 1,480 record copies. Every record gets the same bytes, so it now copies disjoint targets after one barrier
  and keeps the per-record barrier only when two targets share a byte. In the mining tunnel the average replay goes
  from 5.86 to 5.05 ms (4.32 ms without the reset), and a run with the validation layer reports no error.
- **Ordinary geometry shaders translate and cache** (#49 and #50 by monsterlag, beec7c4,
  [#45](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/45)). A geometry stage request now names its input
  topology. The translator checks it against the stage's header and returns the output topology, invocation count
  and maximum output vertices, and the cache keeps that contract next to the SPIR-V. The other stages keep their code
  hashes and input ids. Passthrough geometry stages are still refused, because the translator can't see which
  varyings the next stage needs, and the terrain's geometry stage is one of them. No geometry stage is replayed yet.
  `--probe-shader-geometry` runs all five input topologies through the real translator and cache. A tutorial run
  with both merged gave 10,573 frames with no validation error and no shader failure.
- **The terrain's indirect records are reset in replay order** (#48 by monsterlag, 629ceea,
  [#45](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/45)). Before the destructible terrain draws, a point
  draw resets each piece's 5-word indexed indirect record to {0, 1, 0, 0, 0} by storing through pointers it loads from
  a list. Those stores are lost in the translated shader, so the records stayed zero on a fresh load and held stale
  memory later, the garbage arguments of the respawn stall in #10. The host now recognises the reset program, reads
  the pointer list from its GPU-visible backing at that point of the replay, checks every target, and writes the
  records itself, as Eglantine does. In the mining tunnel each reset writes about 1,480 records. The terrain is still
  black: the records' index counts come from the geometry stage, which isn't run yet. Replay time in the tunnel rose
  from 4.3 to 5.9 ms on average, with the frame still at 60 FPS; the cost points are listed in the review.
  `TOTK_FIX_INDIRECT_RESET=0` turns it off.
- **Compute storage bases from guarded runtime candidates** (#42 by monsterlag, e5309ca). Storage buffers whose base
  address a compute shader reads from c1, from the pushed roots or from bank 17 are resolved at run time against the
  candidates the dispatch binds, and the shader checks the true range. No bank needs a global preference.
- **Descriptors resolved through their active pools** (#46 by monsterlag, 0c6f5c7). Equal descriptor indices in
  different texture or sampler pools used to alias one registration; the descriptor-pool probe passes 60 checks. An
  A/B in the tutorial showed no cost.
- **Geometry inputs reflected without the per-vertex array** (#47 by monsterlag, e62dc53), a prerequisite for running
  the terrain's geometry stage.
- **Audio checked during gameplay.** With `TOTK_TRACE=audio`, a 90 s tutorial run shows sound from about 20 s in, an
  RMS of 0.02 to 0.04 with peaks up to 0.41, and no underrun or overrun on the SDL3 device.
- **Copies between decoded ASTC textures** (#43 by monsterlag, e90bc7c). A texture-to-texture copy of an ASTC
  texture used to be skipped because the image holds decoded RGBA8 and not the guest's blocks. It now copies the
  decoded texels between two images of the same format when the region is aligned to the blocks, which ASTC
  requires, and skips any other case, as before. The copy probe passes 14 of 14 cases.
- **The respawn stall draws garbage** ([#10](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/10)). philneitz's
  run with the new line reads back every indirect argument of the slow parts: the largest asks for 2,309,897,600
  indices from index 55,377,280, and each part adds up to about 2.2 trillion indices x instances. No real draw
  looks like that. The compute stage that gets a zero storage buffer from c1 is not the writer: it only reads that
  buffer and is not among the slow parts' stages. The slow-part line now names the shader that last stored over the
  largest draw's arguments in that part, or says none did (a38643c).
- **Guide movies loop after a Continue** (15152da). At the end of a movie the player flushes and starts again with
  the two buffers it still holds, and submits its third packet before it releases one. The decoder refused that
  packet, nothing released a buffer after that, and the guide window kept the movie's first picture, a flat yellow
  card, for the rest of the run. Packets now wait in a queue of eight, as in Eglantine's video session, until a
  buffer is reserved. In a resumed run the movie looped 16 times, where it had stopped after 183 pictures.
  `TOTK_TRACE=codec` logs the decoder calls.
- **A slow GPU part reads back its indirect draws' arguments** (f876d9b,
  [#10](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/10)). In philneitz's respawn stall, 2,755 of the
  3,197 draws in an 18.5 s part were indirect, and the slow-part line did not show their counts. It now logs the
  total and the largest indirect draw, read once the GPU is done with the part.
- **Test fixtures fixed** (#41 by monsterlag, 41bb451). The profiling calls added for the slow-part line had broken
  two CPU fixtures: the indirect dispatch test (no `note_work`) and the resource frontier test (the upload timing's
  Vulkan calls). Both pass again.
- **Saving and loading work.** During a 600 s run of the scripted test the game wrote `progress.sav`, `caption.sav`
  and `config.sav` through three commits. A second run on a copy of that profile showed Continue and New Game on the
  title screen, and Continue resumed in the mining tunnel of the tutorial, where the first run had saved.
- **The 20 fps stretch is gone** (69d51d0, [#10](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/10)). The
  present thread held each present until the pacer's display time. While it slept, the next present waited behind it,
  and so did every drain of pending presents. That includes the reads Bananza's job workers make of GPU-written
  storage in the middle of a frame: profiles put 13 to 19 % of their time there. The vsync wait counted as game work,
  and the game's frame-rate controller asked for 3 vsyncs. Bananza now sends the present once its blit is submitted
  and keeps only the pacing in `nvnWindowAcquireTexture`; `TOTK_PRESENT_HOLD=1` brings the hold back, and TotK keeps
  it. In a 600 s run of the scripted test, every minute after the first ran at 59.4 to 60.0 fps (1 % low 40.8 to 57.1),
  where minutes 2 and 3 ran at 20.0 and 26.6 fps before, and the game changed its present interval 28 times in all.
  The run had no fault.
- **A replay part that runs long on the GPU names its work** (d4c27e2,
  [#10](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/10)). philneitz's log of the respawn stall showed the
  GPU itself busy for about 16.8 s per frame inside the replay, with uploads and the present near zero and no device
  loss. With `TOTK_TRACE=perf`, a part that runs at least `TOTK_PERF_SLOW_GPU_MS` (default 100) on the GPU now logs
  its draw and dispatch counts, its largest direct one and the NVN 2 names of its stages.
- **ASTC chunk breadcrumbs are written by the recorder thread** (#40, by monsterlag). They used to go into the command
  buffer from the submitting thread while the recorder thread owned it, which Khronos validation reported as two
  threading errors. The PR's new `--probe-vkcopy-astc` passes 5 of 5 cases and 5,526 checks with 0 validation errors
  on an RTX 5060 Ti as well.
- **The #9 skip no longer slows every draw** ([#10](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/10)). The
  skip of the later GPU fault (6da41ba) looks up the NVN 2 name of each draw's vertex stage, and that lookup scanned
  the whole lowering cache under a lock. A profile of the render thread put 72 % of its samples there. In heavy
  scenes the replay took about 74 ms a frame, and Bananza's own frame-rate controller answered by asking for 20 fps. The
  name now comes from an index of lowered code buffers. In one 420 s run of the scripted test, the game presented
  19,817 frames where it had presented 12,297 before, asked for 60 fps on 86 % of them, and the replay averaged 5.6 ms a
  frame instead of 29.3 ms (worst window 13 ms, down from 114 ms). The light quad is still skipped, and the run had no
  fault. From about 43 s to 163 s of the test the game still asked for 20 fps while the render thread was busy only
  about 8 ms a frame; the host's present hold caused that (fixed in 69d51d0, above).
- **Every GPU submission is timed on the perf line** (eb542bc, 6a49228). Upload batches, which also carry the ASTC
  decoding of texture uploads, and the present (blit, colour adjustment, FSR, overlay) went to the queue without
  timestamps. With `TOTK_TRACE=perf` the perf line now gives their GPU time per frame, the batches and bytes staged,
  and the time threads wait for uploads. It is for the respawn stall in the Ingot Isle Cave-In section
  ([#10](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/10)), where the GPU ran at 100 % while the timed parts
  showed about 0.6 ms of a 16.8 s frame.
- **ASTC textures written by buffer-to-image copies decode on the GPU** (#38, by Kvinkxerli). Such a copy into an ASTC
  texture used to be skipped; with the GPU decoder on, tight copies now go through it in the replay, which then
  rebinds its own compute state. Copies with padded rows are still skipped. Kvinkxerli's run got through the Ingot
  Isle Cave-In cutscene to the Baboonburg save point with it. In the tutorial here, the one ASTC copy still skipped is a
  different kind (texture to texture, clear or readback), and 2 runs of 600 s showed no regression.
- **The later GPU fault is skipped too** (6da41ba, [#9](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/9)).
  In 9 runs of 600 s, every run where one more light quad (NVN 2 vertex stage cae0ef8e9fe9147f) drew faulted, with its
  draw in flight at the device loss, and the runs where it never drew did not. Its fragment stage does not read
  c0+0x530, so the first skip missed it. It is now skipped on NVIDIA as well, and 6 runs of 600 s since had no fault,
  including one where it came up. Three quads that draw in every run without a fault have the same vertex input (an
  input at Location 9, every attribute per instance) and the same integer code, so it is matched by name and the cause
  is still open. Skipping every quad of that shape left out ten vertex stages that never faulted.
- **Small Switch 2 reads can skip the GPU wait, as an opt-in** (#24, by NotShinySource, with monsterlag's opt-in).
  Bananza invalidates 128 bytes of a pool every frame, and with the asynchronous replay that read waited about 15 ms
  for the frame's GPU work. With `nx2_read_nowait = 1` and `replay_async = 1`, a read of up to 4 KiB no longer waits
  and its write-back is queued after the submissions it would have waited for. It stays off by default because that
  write-back can overwrite CPU writes made after the invalidate returns. The same PR fixes the replay thread counting
  release items as finished presents, which could end the one-frame present bound early.
- **Partial stencil clears are replayed** (#36, by monsterlag). The mining tunnel clears stencil with write mask
  `0x1`, which the host used to skip. Vulkan's clears ignore the write mask, so a full-screen triangle now replaces
  only the masked bits, in its own pass per layer, leaving depth and the other stencil bits alone. A 600 s run here
  had no fault and 0 validation errors. Nobody has shown yet whether this changes the black cave surfaces.
- **Timestamp reports keep their submission's tick** (#37, by monsterlag). The tick a report carries used to be
  looked up in a 1,024-slot ring by fence value, so a report that completed before the submit published its slot, or
  after another fence reused it, fell back to the completion time and could run backwards (100 ms, then 20 ms). The
  tick now travels with the submission to each report.
- **The later GPU fault comes from one stage pair** ([#9](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/9)).
  Logging every distinct stage pair showed that the runs where one instanced quad's vertex shader drew all faulted,
  4 of 4, and the two where it never came up didn't. Leaving out only that vertex stage gave 0 faults in 4 runs of
  600 s. Nothing in its SPIR-V is rare; what it shares with the early light quads is still being narrowed down.
- **Guest thread exits check their jump frame** (#34, by monsterlag). After #33, both thread entry points stop with a
  clear error if the CRT's `setjmp` recorded no frame, instead of failing fast later in `longjmp`.
- **The later GPU fault is not about pipeline timing.** With pipelines built synchronously (`TOTK_PIPE_SYNC=1`,
  `TOTK_PIPE_WAIT=draw`, as `--shot` does), 2 of 4 runs of 600 s still faulted around 7 minutes in, the same rate as
  with background builds. The vertex shader on the GPU changes from fault to fault
  ([#9](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/9)).
- **Furthest point so far** (philneitz, by hand on an RTX 5090 Laptop): through the storm cutscene and the cave-in to
  the Ingot Isle Cave-In section, where dying and respawning stalled the GPU at about 0.1 FPS
  ([#10](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/10)).
- **Compute push constants land where the shaders read them** (#20, by monsterlag). NVN 2 packs a compute program's
  root tables into c0 after its 16-byte head, each padded to 16 bytes, before the buffer tables; the shader control
  gives each table's word count. A test run showed the game pushing 32 bytes into root table 0 of the upscaler: its two
  image rectangles (`{0, 0, 1919, 1079}` at 1080p, changing with the dynamic resolution), which the host used to guess
  from the bound images. The tutorial looks the same; the translator has to be rebuilt
  ([#2](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/2)).
- **ExitThread works with Windows context IP validation** (#33, by zswxp59). The thread exit's `longjmp` now uses
  MinGW's SEH-aware `setjmp`; the non-SEH form failed with `FAST_FAIL_INVALID_SET_OF_CONTEXT` on Windows 26300
  ([#31](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/31)).
- **An installer for Bananza** (#19, by monsterlag). From your own base 1.0.0 ExeFS and RomFS folders it packs the
  RomFS, builds the shader translator, lifts the code with the patch table and builds `dkb.exe`, from this checkout
  with the toolchain listed in [installer.md](installer.md).
- **The GPU fault after the first guide movie is not one shader's.** Leaving out the vertex shader that faults moves
  the fault to another vertex shader of the same instanced light-quad family. Three NVIDIA drivers hit it at the same
  point: 621.01 and 617.42 with an invalid instruction pointer, 617.14 with a crash inside the driver's shader
  compiler ([#9](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/9)).
- **A mitigation for that fault** (84a8f7d). On NVIDIA, the light-quad draws whose vertex stage fetches ten
  per-instance streams are skipped (`TOTK_FIX_VS_FAULT=0` draws them). The rest of the family draws, so the scene
  keeps its lighting. In two 600 s runs, one played to the end (30 FPS in its last minutes) and one faulted later,
  deep in the mining tunnel; without it every run faults right after the first movie.
- **The tutorial's guide movies play** (f5f1a23). Their player sends packets until one comes back not ready, then
  waits on the decoder's submission-ready event, which `nn::codec` signalled only once. The player stopped at its
  second picture, the flat yellow card the movies open with, so the guide window showed a yellow box. Reserving an
  output buffer now signals the event, and the window shows the movie.
- **Contributors' fixes merged**: the Switch 2 counter at 31.25 MHz (#25), timestamp reports at submit time (#26),
  the README's missing patch table (#16), the replay drain (#23), compute overlap mode (#18), interface array
  reflection (#21) and IPC session lifetime (#17). The tutorial still runs at 20 FPS most of the time here; one run
  reached 30 FPS for its last minute. The game's own pacer accounts for most of each frame.

## 2 November 2026

- **Ten minutes into the tutorial with a workaround for the GPU fault.** Dropping the lighting draws whose fragment
  shader reads the storage buffer at c0+0x530 (a local diagnostic switch, not committed) lets a 600 s scripted run
  (walk, camera, punches) play the guide movie and go on into the mining-tunnel tutorial with no fault and no hang.
  The scene is dark without those draws, and the guide-movie window shows a flat yellow box. VRAM pressure is ruled
  out as the cause: at the fault the device-local heap is at 2666 MiB of 7123 MiB
  ([#9](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/9)).
- **Block-linear textures use the Switch 2 GOB layout** (f4f7b17). Inside a 64 x 8 GOB, Switch 2 swaps bits 5 and 6
  of the offset, as Eglantine's `NativeGobOffset` does for every nx2 image. With the Switch 1 layout, textures read
  straight from the game files came out scrambled; the gold rock in the tutorial cave now has its crystal facets
  ([#5](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/5)). Textures the copy engine writes round-trip
  through the same layout, so they looked right either way.
- **The freeze after the first guide movie is the same GPU fault.** A debugger on a frozen run found a game thread
  spinning in `vk_upload_flush` (`runtime/vk_memory.c`): the device was already lost, `vkWaitForFences` returned at
  once and the loop never checked it. The wait now reports the loss, and 4 of 4 runs end with the same device fault
  (invalid instruction pointer) instead of a freeze. The fault itself is still open
  ([#9](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/9)).
- **Eglantine is the default reference** for NVN 2, GPU and Switch 2 service bugs; `tools/fetch_refs.sh` fetches it.
  Compared at this fault: it serves the movie through the `gsv` service, and at its default GPU accuracy it does not
  copy GPU-written textures back on CPU reads, which we do.
- **NVN 2 push constants recorded** (33628c7). A run confirmed the lead: the game pushes `{1/w, 1/h, w, h}` into
  graphics root table 0, exactly what its full-screen blit shaders read from bank 24, and the replay now binds those
  values instead of a guessed block. No compute shader reads bank 24, so where compute reads its pushes is still open
  ([#2](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/2)).
- **The GPU fault after the first guide movie is not solved.** Translator patch 0013 alone did not stop it; with push
  constants in place, one run of two faulted and the other froze, both right after the first movie frame. The fault
  type (invalid instruction pointer) now points at shader code that goes away while the GPU still runs it; runs with
  synchronous pipeline builds test that ([#9](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/9)).
- **The repository opens for contributors**: roadmap issue, one issue per open task, a tester guide
  ([dkb-testing.md](dkb-testing.md)) and issue forms for Bananza.
- **Push constants located.** NVN 2 writes them as Ampere root tables, and the only constant bank above 17 the game's
  shaders read is bank 24, in the full-screen blit shaders. The lead is in
  [#2](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/2).
- **GPU fault after the first guide movie: first lead** (eaac181). The game's neural upscaler reads shared memory
  with data-dependent indices, and padding shared memory to 48 KB seemed to stop the fault, but those runs froze
  instead, which the count missed (see above). Translator patch 0013 clamps every shared and local memory index; it
  turned out not to be enough.

## 1 November 2026 (night)

- **Tutorial guide movies play** (e9a1feb). Punching in the tutorial opens a VP9 movie through the Switch 2
  `nn::codec` API; the host now decodes it with libvpx, following the contract of the game's own player.
- **Gameplay input**: Donkey Kong answers the controller in the tutorial cave.
- **The 3D scene renders** (5c1a10d): the tutorial cave, gold, grass and light shafts, 2,600 frames with no Vulkan
  validation error. Two causes: NVN 2 packs the compute driver bank per program, and the game uses textures as storage
  images without asking for it.
- **Every shader of the first 3D scene translates**: 4,820 of 4,820 Ampere stages lower to Maxwell and become SPIR-V.

## 1 November 2026 (afternoon)

- **First picture** (3a3e5a0): the first-boot mode menu, 40 s after start. The executable becomes `dkb.exe`.
- **Vulkan runs without a device loss** (c935940).
- **First Ampere shaders reach SPIR-V** (f080197): Eglantine's Ampere-to-Maxwell lowering runs when the game sets a
  program, and TotK Recomp's translator does the rest. DKB Recomp becomes GPL-3.0-or-later for it (9b13ff1).
- **Repository published.**

## 1 November 2026 (morning)

- **The main loop runs on a null renderer** (39d099c, f0e0fa3): 20 fps, audio playing, controller input and rumble.
- **Boot into NVN 2** (c34fe11, 2d7588d): through `nn::init`, 2,345 constructors, `nnMain`, the RomFS (75,238 files)
  and the save, up to the NVN 2 bootstrap (driver ABI 231.16).
- **The whole game lifts** (3941578): `main` and `sdk`, 180,061 functions, 585 MB of C with no unsupported
  instruction, after adding ARMv8.3 pointer authentication and `LDAPR`. The executable compiles and links.
