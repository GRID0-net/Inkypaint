# Independent render rate research

Status: **experimental scheduling and camera interpolation implemented, skeletal interpolation and a reported
horizontal seam remain unresolved** (2026-10-26). The current stopping point, failed fixes, latest code paths and
user feedback are documented at the top of `handoff.md`; that continuation supersedes the older probe notes below.
Normal launches and the packaged launcher retain their usual behavior. There is no launcher setting for this yet.
The experiment is explicitly enabled by `INKYPAINT_RENDER_HZ=120`. `INKYPAINT_FRAME_TRACE=1` additionally logs task
names and graph descriptors. Neither variable is written into the user's configuration.

The requested feature is real rendering from interpolated camera, actor and skeletal state at up to 120 Hz, while
simulation remains at 60 Hz. Image blending, optical-flow frame generation, repeated presentation and running all
updates twice as often do not meet that requirement.

## Per-task scheduler follow-up

Skipping the native alternate branch was insufficient: graph `+0x4c8` still ran Actor, Physics and AI-related
modules on those frames. The current experiment instead preserves graph construction, dependency bookkeeping and
completion, and gates each registered job's **execution callback**. It returns to LR while leaving the native
scheduler to complete the node normally. This avoids the null graph dependencies caused by skipping whole calc.

The job descriptor holds its object pointer at `+8`. Runtime nodes copy this pointer, and the worker calls virtual
method `vtable + 0x10` at `0xcad8a70`. The graphs observed in a normal 50-second run were:

| Module-system field | Observed contents |
| --- | --- |
| `+0x4d0` | Controller, GameFrame, GameNet, Camera and system modules |
| `+0x4c8` | Actor, Physics, GamePhysics, scene, game camera, UI, audio and application modules |
| `+0x4d8` | GSys render jobs, actor background graphics/base-proc jobs, AI-group jobs, camera, dynamic lights, network receive |
| `+0x4e0` | Post-draw GSys jobs and GameGfx wrapper |

The experiment gates `4c8`/`4d0` tasks and network receive to the fixed-step clock, keeping Graphics and Resource
maintenance active. **This is a working scheduling boundary, not a complete classification of every simulation
operation.** In particular, AI-group and base-proc callbacks in `4d8` still need their phase semantics audited.
The reported rates count entry to the registered job callbacks, not every nested update or an independently
measured gameplay-speed comparison.

`inkypaint_frame_clock.h` supplies a 60 Hz clock independent of presentation rate. It preserves fractional tick
phase, uses the framework's six-step native catch-up loop, and reports a history reset when a suspension exceeds
that bound. The clock test covers 60/90/120/144 Hz sampling, half-tick fractions, catch-up, long suspensions,
backward time and large timestamps. The camera prototype now consumes per-job copies of its interpolation fraction, as described in `handoff.md`.

The hooks remain inactive by default, validate all hooked entry-point signatures before installation, and reject
unknown graph/executor types in the scheduling experiment. Job registration and callback decisions are protected
by a mutex. Unknown executable versions are left alone. `present_pace` uses the experiment's target only while it
is explicitly active. The normal runtime, previous graphics/online fixes, launcher app and saved settings remain
available.

Verification in this follow-up:

- `logs/framerate-job-graphs.log`: normal timing, graph/name discovery.
- `logs/framerate-task-gate-test.log`: initial 40-second half-resolution scheduling run, approximately 100 presents/s
  and roughly 60/s for the measured simulation-task entries. Its black plaza capture is **not** visual verification.
- `logs/framerate-baseline-plaza.log`: ordinary native-resolution run with synchronous pipelines, visible plaza and
  player, normal exit.
- `logs/framerate-task-gate-warm.log`: 50-second native-resolution scheduling run with the same pipeline setting,
  visible plaza/player and normal exit. Warm intervals reached about 66–70 presents/s while the measured gated
  tasks stayed near 60 calls/s.
- `logs/framerate-task-gate-final.log`: repeat after clock integration and strict executable/task guards.
- `logs/framerate-default-smoke-final.log`: ordinary 30-second launch, experiment inactive, normal exit.
- `inkypaint_frame_clock` CTest passed. The changed scheduler passed `-Wall -Wextra -fsyntax-only` and the repository
  quality checker.

The user confirmed that the first asynchronous shader pass can leave the plaza black until it is reloaded. This
also occurred in baseline captures. Synchronous pipelines were a command-line-only control for the visual
comparison, and the saved asynchronous-compilation preference stays enabled. Presentation counters above a
panel's refresh rate do not prove that the panel displayed that many distinct poses. No 120 Hz smoothness claim
has been established.

## Mechanisms established

`present_pace` in `src/nx/runtime/present.c` waits in VI buffer dequeue on the guest thread. Its existing `fps_limit`
controls the game's production of frames, not an independent rendering clock. Raising this limit alone does not
preserve simulation timing.

The 11.3.0 executable has these framework entry points. These are addresses and behavioral observations, not
exported game code. They intentionally apply to this exact version only.

| Address | Observed role |
| --- | --- |
| `0xc5316d4` | Base framework frame execution, draw followed by calc and synchronization |
| `0xc5317dc` | Draw preparation and `RootDraw` traversal |
| `0xc531ba4` | Task manager maintenance and `RootCalc` traversal |
| `0x925b1b0` | Application calc wrapper, missed-frame multiplier and time-scale setup |
| `0xcd94b34` | Application frame wrapper, frame-rate controller before base frame execution |
| `0xc533f50` | Method-tree recursive call |
| `0xcda80d0` | `ModuleSystem::moduleCalc_`, schedules and joins task graphs |
| `0xcda8500` | Module task-graph execution, with different graph selections and work masks |
| `0xc532090` | Frame synchronization, including whole-vblank software waits and GPU synchronization |

The live method tree separates these names:

- `sead::RootDraw`, `sead::AppDraw`, `ModuleSystem`, `gsys::SystemTask::DrawTV`.
- `sead::SysDraw`, `gsys::SystemTask::PostDrawTV`.
- `sead::RootCalc`, `sead::SysCalc`, `sead::AppCalc`, `ModuleSystem`.
- `gsys::SystemTask::PreCalc`, `ModuleSystem::moduleCalc_`, `gsys::SystemTask::PostCalc`.

The high-level split agrees with the public [sead framework declarations](https://github.com/open-ead/sead/blob/master/include/framework/nx/seadGameFrameworkNx.h)
and [method-tree traversal](https://github.com/open-ead/sead/blob/master/modules/src/framework/seadMethodTree.cpp).
Those declarations were read for orientation. No code was copied from that project.

## Experiments and limits

All runs were sequential, with network and GRID0+ disabled by command-line overrides, 0.5x resolution and vsync off.
The user's normal resolution, vsync, FPS and online configuration were retained. The launcher bundle was not replaced
with an experiment.

1. Skipping the complete calc wrapper is invalid. It also suppresses graphics-job preparation that the next draw
   and synchronization phase require. After correcting a skipped GuestFn's required `CPU.pc = LR` return, the run
   exposed a null dependency during task-graph setup, inside `0xcad8520`. The first return-path attempt had spun in
   dispatch until its deadline instead of returning, so its enormous skipped-call counts are not usable measurements.
2. Skipping only `ModuleSystem::moduleCalc_` still omits required render preparation and causes the same dependency
   failure. Preserving the surrounding `PreCalc` and `PostCalc` callbacks is insufficient.
3. Allowing all updates while requesting 120 presentations produced more than 60 draw calls per second but also
   advanced application updates above 60. This is an unsafe uncapping experiment, not a speed-correct feature.
4. Keeping the module callback active while selecting its existing alternate task-graph branch with bit 1 at
   `ModuleSystem + 0x4f0` avoided the dependency failure. A deadline gate selected the ordinary branch at roughly
   60 calls/s and the alternate branch between them. This proves a potentially useful scheduling boundary exists.
   **It does not prove that every physics, animation, timer, input and network update is confined to that branch.**
   The bit's complete semantics still require tracing the graph descriptors and their callbacks.
5. The framework also waits in whole console vblank intervals (`framework + 0x88`). Suppressing that software wait,
   while retaining the GPU synchronization code, raised the observed draw rate in the probe. The final 35-second
   run reported about 71–76 draws/s and 59.9–60.6 ordinary-branch calls/s after loading, at a requested 120 Hz.
   It exited normally. It did not achieve or prove 120 distinct rendered poses.

Evidence:

| Local log | Result |
| --- | --- |
| `logs/framerate-phase-test.log` | Incorrect skipped-function return path, not valid timing evidence |
| `logs/framerate-phase-test2.log` | Complete update skip exposed missing graphics dependency |
| `logs/framerate-tree-probe2.log` | Live method tree, normal updates above 60 under uncapping |
| `logs/framerate-module-probe.log` | Module callback skip exposed missing graphics dependency |
| `logs/framerate-graph-probe.log` | Alternate graph branch, no crash during the 30-second run |
| `logs/framerate-pacing-test.log` | Alternate branch plus pacing change, 35 seconds, normal exit |
| `logs/framerate-restored-smoke.log` | Verification after restoring the normal runtime |

## Work still required

Complete the callback classification, including AI-group and actor base-proc phases in graph `0x4d8`.
Use the engine's graph path to preserve every required render job without advancing simulation-only callbacks.
A wrapper's call count is not a simulation-clock verification.

Then capture previous/current visual states with stable object identities and lifetimes. Camera transforms and
skeletal matrices must be interpolated into render-only storage, leaving gameplay, collision and networking state
untouched. Renderer work must carry its simulation tick and interpolation fraction through asynchronous CPU job
execution and GPU submission. A mutable global alpha sampled later by the GPU thread is insufficient. Scene changes,
teleports, destroyed objects and topology changes must invalidate history.

Verify distinct intermediate geometry and normal game speed against the 60 Hz baseline, including movement, jump
and weapon timing, UI navigation, scene transitions and GRID0+ idle/lobby activity. The current probe has no pose
interpolation and is deliberately absent from shipped runtime settings.
