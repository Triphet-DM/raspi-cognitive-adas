# PROJECT STATUS

> Source of truth / high-level dashboard for raspi_project v11.
> Overwritten whenever a major architectural decision changes.
> Detailed history lives in the dated session reports in `debug_reports/`.

**Last updated:** 2026-06-17
**Branch:** `fix-gil`
**HEAD:** `72bd460 feat(audio): complete L1-L4 shadow pipeline with speaker integration`
**Remote:** pushed to `origin/fix-gil` (in sync).
**Direction:** **Cognitive Driver Assistance** — non-speed behavior **architecture FROZEN
2026-06-15** (see next section). Speed L1–L4 behavior unchanged.
**Perf note (2026-06-16):** speed investigation found **async halves throughput** — run
production in **SYNC**: ~19 FPS @512 (was ~10 async), CPU 80%→50-60%, zero accuracy/behaviour
change. int8 ruled out on CPU. Detail: `FP32_SPEED_ENVELOPE.md`, `INT8_AB_RESULTS.md`.
**imgsz decision (2026-06-17):** stay at **512** — ~19 FPS sync is sufficient for real-time
now; **416 / B3 live-detect check CUT** (not needed). B2 real-drive soak deferred (can't do
yet). B4 systemd parked. So the only perf change that ships = **drop async** (512 locked).
**Bench (2026-06-17):** all 5 speed shadow scenarios PASS; found K≥2↔voter-cooldown coupling
(see Known Issues) — to be resolved by the cutover step below, not a bug.

---

## Non-Speed Architecture — FROZEN 2026-06-15 (Attention Arbitration)

> Architecture phase complete. Detail: `2026-06-15_session_report.md`. Only numeric tuning
> of the attention scale remains, and that is a **bench-phase** task, not architecture.

This is a **real-time attention scheduler** on edge hardware: the contended resource is
the **driver's attention channel** (one audio notification at a time), not CPU.

### Two-Brain Architecture
- **Brain 1 — PersistentState Engine** (DONE): speed family, `L1→L2→L3→ shared L4`.
  State persists after the sign is gone; a missed announce is **re-derived** later.
- **Brain 2 — Momentary Engine** (designed): all 10 non-speed signs. **No episode
  lifecycle.** Per-class notification timestamp (Human Memory Suppression Model):
  `now − last_notified[class] ≥ suppression_window[class]` → else SUPPRESS → emit Action.
- **Key asymmetry:** momentary info has **no second chance** (drive past → dies) so it may
  **interrupt**; persistent state is **re-derivable** so speed does **not** need interrupt.

### Layers (after `TemporalVoter` confirm)
`BehaviorPolicyRouter` (which brain) → engine → `Notification Arbiter` (stateful, resolves
the speech-channel conflict; preempt = **kill the playing `aplay` mid-clip**) → **shared
L4** (single-slot latest-wins) → speaker.

### Attention Arbitration (the core decision)
One policy field collapses the old `priority` + `interrupt_level`:
`MomentaryPolicy { suppression_window, attention_rank }` + global `INTERRUPT_THRESHOLD`.
- **SELECTION** (idle): pick highest `attention_rank`.
- **PREEMPTION** (active): interrupt iff `incoming.rank > current.rank AND incoming.rank ≥
  INTERRUPT_THRESHOLD`.
- **`INTERRUPT_THRESHOLD` = Safety Boundary** (= lowest life-safety rank, School Zone) ⇒
  "may interrupt active speech" ⟺ "is life-safety" ⇒ **re-derives Law 2.**
- One axis **forces** selection-order = interrupt-order (no incoherence). Sparse scale ⇒
  new classes insert at one number, nothing else moves.

### Full attention ranking (structure LOCKED; numbers PROVISIONAL — tune at bench)
Pedestrian Crossing 30 · Pedestrian Warning 25 · **School Zone 20 = threshold** · Speed
CHANGE 12 · Curve Ahead 10 · Crossroad/Traffic Signal 8 · No Parking/U-turn/Stop/Passing 4
· Speed REMINDER 2. **Speed outputs share the channel and carry ranks too.**

### Three temporal mechanisms (renamed; different natures — do not call all "cooldown")
**Detection Stability Window** = K/N **frames** (count, not time) · **Persistent Reminder
Interval** = 180 s (speed only) · **Notification Suppression Window** = per-class momentary.

### Rejected this session
`interrupt_flag` boolean · separate `priority`+`interrupt_level` · `can_preempt` boolean
(GPT withdrew) · shared safety cooldown (per-class windows already prevent it).

### Integration refactors that touch the done speed engine (TO-BUILD)
1. Extract `L4`/NotificationManager out of `ShadowSpeedLimitPipeline` → one shared L4.
2. Two decision→wav maps (`SpeedAudioMap` value→wav stays; momentary needs class→wav).
3. Preemption requires killing the running `aplay` subprocess (latest-wins only replaces
   the queued slot, never the playing clip) — and **interrupt awareness**: the Arbiter must
   signal the engine which clip failed to finish.

### Delivery Completeness Model (refinement, pending Arbiter)
A speed clip **killed mid-sentence by a preempt was NOT "successfully delivered"** ⇒ it is
eligible for **re-delivery** once the safety clip ends (correct reading of the Notification
Completeness Principle, not a new law). A clip that **played to completion** before a later
safety event → **no** re-delivery. `re-arm (L1) ≠ re-announce (L3)`: temporary
disappearance is already handled correctly by the L3 reminder-cooldown gate — **do not
touch L1 re-arm**. Replaces the withdrawn "Attention Context Reset". **Does not block speed
cutover.** Detail: `2026-06-15_session_report.md` §7b.

---

## Project Direction — Cognitive Driver Assistance (behavior-first, since 2026-06-14)

> Original reframe from the 2026-06-14 design meeting. Detail: `2026-06-14_session_report.md`.
> Speed (L1–L4) is unchanged and is now seen as the completed **PersistentState** family.

The project is no longer "AI Traffic Sign Detection" (detect → announce). It is a
**Cognitive Driver Assistance** system: **detect → decide whether the driver should be
interrupted → only then speak.** It does NOT drive; it performs **attention redirection**
for a **human-in-the-loop** driver who makes the final decision. Central question shifted
from *"can I detect more signs?"* → ***"which information deserves the driver's attention
right now?"*** **Meta principle: maximize driver comprehension, NOT information delivery.**

### Behavioral Laws (locked)
1. You don't have to say everything (report all ⇒ driver stops listening).
2. Life-safety signs override everything — human safety > communication consistency
   (Safety tier only may interrupt).
3. One episode = one notification (long visibility ≠ repeat).
4. No queue anywhere — drop stale, re-derive from current state.
5. **Truth validation belongs exclusively to the Perception layer**; the Behavior layer
   does not re-validate perception truth. *(NOT "perception is 100% correct" — it isn't;
   trust-filtering simply isn't Behavior's job.)*
6. The driver's intake is limited — usually speak one thing at a time.
7. Reliability is everything (anti cry-wolf): false alarms train the driver to ignore us.
8. **Machine attention capacity is intentionally limited** — behave as if the machine
   itself can focus on only one important thing now. (Generator of suppression /
   arbitration / cooldown / interrupt / stale-rejection.)

### Current behavioral grouping — SUPERSEDED 2026-06-15 (historical)
> The 4-family semantic grouping below was a 06-14 stepping stone. It is **superseded** by
> the two-brain + attention-rank model in the FROZEN section above (semantic families
> collapsed: one Momentary engine + per-class `attention_rank`). Kept for history.

Grouped by behavior pattern. 3 momentary families **share one mechanism, differ only in
policy** (priority/cooldown/suppression/interrupt) — not 9 subsystems, not one monolith.

| Family | Members | Stack |
|---|---|---|
| **PersistentState** (Speed, done) | sign_50/60/80/90/100 | L1+L2+L3+L4 |
| **Safety** (human life) | Pedestrian_Warning, Pedestrian_crossing, School_Zone\* | momentary |
| **Warning** (road hazard) | curve_ahead, sign_four_way, Traffic_sign\*\* | momentary |
| **Restriction** (prohibition) | no_parking, no_passing, no_stop, no_u_turn | momentary |

\* School_Zone provisionally Safety/momentary (camera-only has no end-of-zone sign / no
GPS); revisit if bench shows pseudo-persistent-zone behavior. \*\* `Traffic_sign` = a
static *"traffic light ahead"* warning sign, NOT a live color-changing signal.

### Behavior decisions (Q1–Q8) — summary (detail in session report)
Q1 interrupt-immediately (Safety only) · Q2 perception=truth / behavior=usefulness ·
Q3 tier≠identity, speak again, cooldown per-class · Q4 one-episode-one-notification ·
Q5 suppress at overload, speak one · **Q6 optimize *net safety benefit*, balance
precision vs reaction-window experimentally** (corrected from "reliability first") ·
Q7 monitor while silent, safety pierces cooldown · Q8 no defer, drop + re-derive.

### Still open before architecture freeze
1. Suppression must bind to **measured** precision, not importance alone (pre-bench:
   measure per-class precision of Safety family). 2. Same-tier tie-break (nearest ROC?). 
3. Suppression = {K_confirm, cooldown} (proposed). 4. cooldown↔L1 re-arm (R6) per family.
5. Cross-class arbiter is stateful (architecture-phase). 6. School_Zone zone-vs-momentary.

### Parked (not decided): buzzer · global 2–3 s cooldown · per-class conf threshold ·
second-stage model (RF-DETR) · exact suppression params · exact family implementation.

---

## Current Status

The **L1–L4 belief-state speed-limit subsystem is complete and verified end-to-end on
Raspberry Pi 5**, including real audio output through the MAX98357A speaker.

- All four leaves + facade implemented, unit-tested, and Pi-verified; full CMake build
  passes; audio plays the subsystem's decisions on hardware.
- The subsystem runs in **SHADOW**: the legacy voter/cooldown path is still the
  **authority** (`[CONFIRMED]`), and **audio follows the shadow L1/L2/L3 decisions only**
  (`--shadow --audio`). **No authority cutover has happened.**
- Production behavior with no flags is unchanged.

What remains before cutover: **systematic bench validation** and a **cutover-readiness**
decision (read `Pending`/`age` telemetry, confirm K=1 vs K=2).

---

## The Architecture (frozen v1)

"Current Speed Limit" is a **belief state to be estimated**, not a perception output.
Four concerns separated: detection / presence / value / announcement.
**Authority split:** YOLO = presence + ROI localization (not value); CLS = value
authority, but only at confirm events.

| Layer | Class | Responsibility |
|---|---|---|
| **L1** | SignEpisodeLifecycle | Armed/Confirmed/Releasing; raw presence; re-arm; emits `EpisodeConfirmed{value, fresh}` |
| **L2** | CurrentSpeedLimitManager | UNKNOWN/ACTIVE belief; commit on CLS value change with K-hysteresis; no-forget |
| **L3** | AnnouncementPolicy | CHANGE / REMINDER / SUPPRESS; global reminder cooldown |
| **L4** | NotificationManager (+ SpeedAudioMap) | map action→wav; single-slot latest-wins; `aplay` on a dedicated thread |

Flow: `perception → L1 → L2 → L3 → L4`, owned by a thin **ShadowSpeedLimitPipeline**
facade (`tick()` on the main/decision thread only → no locks). Plain concrete classes +
facade — **no interfaces, inheritance, event bus, observer, DI, or generics.**

Frozen rules: L1 `fresh=TRUE` only on Armed→Confirmed; re-arm when
`now - last_seen ≥ rearm_after`. L2 acquisition commits immediately (no K), different
value needs K consecutive confirms, same value refreshes + clears pending, **no-forget**,
`age` = telemetry. L3 CHANGE always fires (bypasses cooldown), REMINDER only for a fresh
episode of the same value gated by a global cooldown, continuation → silent, CHANGE
resets the timer. Gapless transitions handled by the value axis. L4 latest-wins,
non-preemptive within category, audio only on `is_announce`.

---

## Stable / Verified (Pi 2026-06-10 unless noted)

- **L1–L4 + facade**: unit tests clean under `g++ -Wall -Wextra` (+ strict set) and
  Pi-verified — **L1 26/26, L2 33/33, L3 23/23, SpeedAudioMap 14/14**.
- **Full CMake `app` build PASS on Pi** (shadow + audio sources compiled in).
- **Audio path PASS**: MAX98357A I2S amp + 3W speaker; 10 WAV assets intelligible;
  `test_audio` PASS; **latest-wins verified** (burst → first + last only).
- **End-to-end PASS**: Camera → YOLO → CLS → L1 → L2 → L3 → L4 → Speaker (audio from
  shadow decisions; legacy authority untouched).
- Prior fixes in place & Pi-verified 2026-06-06: async-camera GIL Save/RestoreThread,
  CameraThread double-buffer per-slot mutex, per-class ROI ownership.
- **Threads=2** best NCNN config (~10 FPS / ~93 ms). FP16 arithmetic = no-op; Vulkan = no
  gain.

---

## Working Tree

- **HEAD `72bd460`, clean, in sync with `origin/fix-gil`.**
- `72bd460` bundled the full L1–L4 working tree (4 decision sources + facade + 2 audio
  sources + 5 tests + 10 WAV assets + `main.cpp`/`Types.h`/CMake edits) **plus** the
  2026-06-09 report and a then-stale copy of this file.
- 2026-06-10: this file repaired + `2026-06-10_session_report.md` added (the only new
  repo content).
- Old `SpeedSignLifecycle.{h,cpp}` retained and compiled — kept as the `[LC-SHADOW]`
  comparison baseline; remove only after validation.

---

## Architecture Decisions (in effect)

- Belief-state model; identity and value orthogonal. YOLO = presence/ROI; CLS = value.
- L2 no-forget; presence loss never changes the value. K-hysteresis default 1,
  configurable, bench-measured.
- **Presence = raw class-agnostic speed-sign presence** from all `detections` (not
  cooldown-filtered, not top-class-only).
- L3 CHANGE-always / REMINDER-on-fresh; reminder cooldown **global, benign default
  180 s** (configurable). CHANGE and REMINDER both reset the timer.
- L4: split pure `SpeedAudioMap` + effectful `NotificationManager`; **single-slot
  latest-wins**; `aplay` subprocess (no ALSA-lib abstraction); audio behind `--audio`
  (+`--shadow`), **non-authoritative**.
- Two timers, two owners: `rearm_after` (L1) ≠ `reminder_cooldown` (L3).
- Object boundaries: plain concrete classes + thin facade, no abstraction machinery.
- At cutover (future): remove voter-input suppression (L3 owns anti-spam); re-sample CLS
  on fresh episode or voter-class-change; skip same-class continuation.

**Flags:** `--shadow`, `--shadow-k <n>`, `--shadow-rearm-ms <ms>`,
`--shadow-reminder-sec <s>`, `--shadow-verbose`, `--audio`,
`--audio-dir <dir=../assets/audio>`, `--audio-device <dev=plughw:0,0>`.

---

## Approved Direction (next steps)

1. ✅ **Bench validation DONE (2026-06-17)** — all 5 scenarios (50-stuck, flicker, gapless
   50→60, re-arm, reminder) PASS. Surfaced the K≥2↔voter-cooldown coupling (Known Issues).
2. **Telemetry read**: `Pending` counts → K=1 vs K=2; `age` distribution → age-display.
   *(Note: a real K≥2 decision is only meaningful AFTER the cutover step below removes the
   voter-input cooldown — until then K≥2 is artificially blocked.)*
3. **Authority cutover readiness** evaluation (honor invariants R3–R5 below).

### Cutover plan — remove voter-input suppression (drafted 2026-06-17)
**Why:** one shared `TemporalVoter` is fed cooldown-gated input → contaminates the shadow
confirm signal → K≥2 can't fire within 5 s. Can't fix in isolation: the same voter feeds the
legacy `[CONFIRMED]` path, which *relies* on cooldown for its own anti-spam → must move
authority to shadow (L3 = the new single anti-spam owner) at the same time. All edits in
`main.cpp`:
1. `:465` feed voter with `top_class` always (drop the `suppressed ? ""` zeroing).
2. `:451-452` drop the `!cooldown.is_suppressed(...)` gate on BestROI update.
3. `:532` retire `cooldown.activate()` + the `[CONFIRMED]` speed path.
4. `:548` + struct `CooldownManager` (`:40-89`): delete the legacy `SpeedSignLifecycle`
   `[LC-SHADOW]` call and the now-dead CooldownManager.
**Anti-spam after:** L3 — CHANGE-always / REMINDER-on-fresh@180 s / **SuppressContinuation**
(`!changed & !fresh` → silent) replaces the 5 s cooldown. **DO NOT touch L1 re-arm.**
**Pros:** K≥2 works at raw (sync ~2×) cadence; single anti-spam owner; deletes a whole legacy
subsystem. **Cons/risks:** confirms now fire continuously → bench-verify SuppressContinuation
catches every `!changed & !fresh`; shared voter means non-speed `[CONFIRMED]` logs get noisier
(cosmetic only — non-speed has NO audio until Brain 2); honor R3–R5. **Does not need Brain 2.**
4. After cutover: remove old `SpeedSignLifecycle` + its `[LC-SHADOW]` call (folded into step 4
   above).

---

## Rejected / Superseded Designs

- **Voter-winner identity** (`SpeedSignLifecycle`) — episode identity tied to unstable
  YOLO sub-class → flicker = false value-change. Superseded by L1–L4; kept only as a
  shadow comparison baseline.
- **STALE state** → cut; replaced by age-as-display (`T_stale` not bench-validatable).
- **Immediate/synchronous audio** (blocks perception) and **FIFO audio queue** (stale
  state) — rejected in favor of single-slot latest-wins.
- **`libasound` + AudioBackend abstraction** for v1 — YAGNI; possible v2 only if
  gapless/preemption is needed.
- Gapless-specific machinery; ROI-quality/proximity re-sample trigger; spatial
  tracking/identity layer — not built.

---

## Current Performance Baseline — re-measured on Pi 2026-06-16 (detail: `FP32_SPEED_ENVELOPE.md`)

**The old "~10 FPS / 93 ms" was the ASYNC path.** A/B on Pi (same 512 model, threads=2):

| Mode | FPS | infer | CPU | notes |
|---|---|---|---|---|
| **sync** (recommended) | **~19** | ~48 ms | **50-60%** | drop async flags → ~2× FPS, free |
| async (`--async-*`) | ~10 | ~98 ms | ~80% | camera thread steals a core → infer 2× |

**Root cause:** `async-camera` spins a CPU-heavy capture thread (≈32 ms/frame) that runs
flat-out dropping unused frames, starving the ncnn inference on the 4-core Pi 5. The 30 ms
camera wait it was meant to hide never happens (infer > camera interval ⇒ a frame is always
ready; sync `cap`=1.9 ms). **Action: run production SYNC** (remove `--async-detect
--async-camera`); behaviour/detection/alerts verified identical (5-min Pi soak; longer soak +
make-default still pending — Diamond).

**imgsz envelope** (camera caps at ~30 FPS for imgsz ≤ 416): 512 = ~19 FPS / mAP50 0.983;
**416 = ~30 FPS / mAP50 0.975 (−0.8%) = sweet spot**; 384/320 give no extra FPS (camera-capped),
only lose accuracy. threads=4 = small free gain when inference-bound (512). Below-416, async,
fp16-arithmetic, Vulkan, int8 = no help.

**int8 ruled out (CPU):** detects fine on Pi but **slower** than fp32+fp16 (A76 fp32 already
efficient + quant/dequant overhead). int8 becomes the right choice only **with an accelerator
(Pi AI HAT / NPU)** — detail + calibration assets kept: `INT8_AB_RESULTS.md`.

**Design budget for Brain 2:** ~19 FPS @512 / ~30 @416, CPU 40-50% headroom. sync/async is
capture-layer only — decision logic (L1–L4, momentary, arbitration) untouched. Frames now arrive
~2× faster: frame-COUNT gates (voting K/N) confirm in half the wall-time (re-check K); TIME-based
gates (rearm, reminder, suppression windows) unaffected — keep "stay quiet" semantics time-based.

---

## Known Issues

- **Repeat-confirmation bug** — fixed by construction in L1–L4; **bench proof pending.**
- **`classify_ms` (Bug #3, LOW)** — diagnosed as rolling-average dilution (only the
  sparse confirm frame is nonzero), **not** a classifier skip. Metric still misleading.
- **TemporalVoter tie-break (Bug #4, LOW)** — alphabetical, not recency. Keep orthogonal.
- **K≥2 blocked by voter-input cooldown (bench-confirmed 2026-06-17, NOT a bug)** — with
  `--shadow-k 2` a value change does **not** announce on first sight; it waits out the 5 s
  cooldown. Root cause: one **shared** `TemporalVoter` is fed cooldown-gated input
  (`main.cpp:465` `vote_input = suppressed ? "" : top_class`). After the 1st confirm,
  `cooldown.activate()` (`main.cpp:532`) zeroes that class's vote input for 5 s, so L2 can
  never collect the 2nd consecutive confirm K≥2 needs until cooldown lifts. **K=1 hides it**
  (L2 commits on the 1st confirm). **Fix = the documented cutover step "remove voter-input
  suppression; L3 owns anti-spam"** — de-contaminates the confirm signal so K≥2 works at the
  raw (sync ~2×) frame cadence. The shadow path's `presence` is already RAW/clean
  (`main.cpp:556-562`); only the `confirmed`/`value` signal is contaminated. **Does not need
  Brain 2.** See cutover plan in `Approved Direction` + R3–R5.
- **`rearm_after` vs cooldown interaction (R6)** — occlusion >600 ms during cooldown
  re-arms L1 → next confirm `fresh=TRUE`. Measure at bench; don't pre-tune.
- **Cutover invariants (R3–R5)** — `reset()` must not be wired to presence loss;
  `onValue` once per episode confirm (not per frame); `tick()` stays single-threaded.
- **Camera-only staleness** — no-forget belief can be wrong after a turn onto an unsigned
  road (no map/GPS). Accepted for v1; surfaced via age-as-display.

---

## Next Immediate Tasks

**Two parallel workstreams now:**

*Speed (PersistentState, existing):*
1. Run the 5 bench validation scenarios; capture logs + audio behavior.
2. Measure `Pending`/`age`; decide K and age-display threshold.
3. Decide authority cutover readiness.

*Non-speed families (new direction — behavior-first):*
4. **Behavior Simulation Round 2** (NOT architecture yet): Scenario A (School_Zone, no
   end-of-zone sign @ 80 km/h), Scenario B (Pedestrian_crossing drops 200 ms then
   reappears 500 ms — same vs new episode), Scenario C (5 signs, suppress 4, driver
   misses a real prohibition — acceptable?).
5. Measure **per-class precision of the Safety family** (gates "low suppression").
6. Only after 4–5: design `SignTypeRouter` + `Priority Arbiter`.

---

## Resume Point For Next Session

- **Read:** this file, then `FP32_SPEED_ENVELOPE.md` + `INT8_AB_RESULTS.md` (2026-06-16 speed
  investigation — async/sync, imgsz, int8), then `2026-06-15_session_report.md` (non-speed
  architecture FROZEN), then `2026-06-14_session_report.md` (behavior laws + Q1–Q8), then
  `2026-06-10_session_report.md` (speed L1–L4 detail).
- **Perf action items (2026-06-16):** (1) ✅ **bench soak PASSED** — sync ran 20 min (+5 min
  earlier) on Pi, behaviour/detection/alerts normal, no GIL hang, no thermal throttle. Real-drive
  soak still recommended before final sign-off, but code/GIL stability is proven. (2) **sync is now the
  way to launch** — currently started by hand, so just omit `--async-detect --async-camera`
  (sync is already the code default). **Future autostart (systemd on power-up, deferred): the
  `ExecStart` MUST NOT include the async flags** — do not copy an old async command, or the 2×
  speedup is silently lost. (3) optionally adopt imgsz 416 for ~30 FPS. Code unchanged (the int8 `--det-int8` experiment was
  fully reverted); this is launch-flag only.
- **Non-speed architecture FROZEN (2026-06-15):** two-brain (Persistent / Momentary);
  BehaviorPolicyRouter; Momentary timestamp model (no episode lifecycle); `attention_rank`
  single axis; INTERRUPT_THRESHOLD = Safety Boundary (re-derives Law 2); cross-engine
  ranking (structure locked, numbers provisional); 3 integration refactors. **Architecture
  phase complete → next is measurement, not design.** Do NOT re-open
  priority/interrupt_level/can_preempt; do NOT add episode lifecycle to momentary signs; do
  NOT fix scale numbers from assumption; do NOT design as if perception is perfect.
- *(Speed-subsystem resume notes below — unchanged.)*
- **Finished:** L1–L4 complete + Pi-verified end-to-end + audio on hardware; pushed
  `72bd460`/`origin/fix-gil`. Shadow only — legacy authority intact.
- **In progress / next:** systematic bench validation (esp. scenario 2 = legacy repeats
  vs shadow silent), telemetry read for K, cutover-readiness evaluation. Use
  `--shadow --audio` (lower `--shadow-reminder-sec` for the bench).
- **Do NOT:** cut over authority before bench passes; delete old `SpeedSignLifecycle`
  yet; add interfaces/inheritance/event-bus/DI/generics; implement STALE; build
  cross-category audio priority; tune road-dependent params from assumptions.
- **Decided already:** authority split, no-forget, K=1 default, CHANGE-always/
  REMINDER-on-fresh, reminder cooldown 180 s benign, STALE cut, gapless-by-value-axis,
  raw class-agnostic presence, single-slot latest-wins, `aplay` v1, audio
  non-authoritative behind `--audio`+`--shadow`, object boundaries.
