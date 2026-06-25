# PROJECT STATUS

> Source of truth / high-level dashboard for raspi_project v11.
> Overwritten whenever a major architectural decision changes.
> Detailed history lives in the dated session reports in `debug_reports/`.

**Last updated:** 2026-06-25 (EOD — benchmark capture + README case-study redesign)
**Branch:** `fix-gil`
**HEAD:** `f53b573 docs(eod): 2026-06-20 session report + PROJECT_STATUS update` (committed work in
sync with `origin/fix-gil`).
**Remote:** committed work in sync. **Uncommitted (06-25):** `README.md` (full redesign),
`docs/demo.jpg`, `.gitignore` (demo/* ignored), `demo/log.txt` untracked. Models/audio/raw-demo
kept local. *(Re-delivery committed `f4f5166` + pushed back on 06-20; the 06-20 docs were stale
pre-commit text, reconciled 06-25.)*
**PRESENTATION (06-24→06-25):** README rebuilt as an **Embedded AI Systems Engineering case study**
(16 sections; architecture/behaviour = identity high, benchmarking/backend/low-precision = star).
Full benchmark suite captured + **post-export consistency proven** (best.pt vs NCNN export, same
test split @ conf0.45 → **Δ ≤0.3%**). Detail: `2026-06-25_session_report.md`. **Operating point:
~18 FPS · 47.8 ms · NCNN fp32 + fp16 storage/packed (NOT fp16 arithmetic) · threads=2 · imgsz 512.**
int8 + fp16-arithmetic ruled out on CPU (slower / no-gain). Dataset = **13,039** labeled imgs / **15 classes**.
**Direction:** **Cognitive Driver Assistance** — non-speed behavior **architecture FROZEN
2026-06-15** (see next section).
**SPEED CUTOVER DONE + Pi-verified 2026-06-17:** L1–L4 is now the speed authority; legacy
`CooldownManager`/`[CONFIRMED]`/`SpeedSignLifecycle` removed; anti-spam owned by L3; K=2 default;
K≥2 no longer blocked by cooldown.
**BRAIN 2 BRING-UP 2026-06-17 + Pi-verified:** MomentaryEngine + MomentaryPolicy +
BehaviorPolicyRouter built & wired; shared L4 extracted (#1) + class→wav `MomentaryAudioMap`
(#2); 10 momentary WAVs recorded; **momentary speech audible on Pi.** Safety re-ranked: School
Zone 30 > Ped Warning 25 > Ped Crossing 20 (=threshold). Detail: `2026-06-17_session_report.md`.
**REFACTOR #3 COMPLETE 2026-06-20:** Notification Arbiter wired — both brains route through it
(commit `e9c8419`); **kill-aplay** via `posix_spawn` — safety interrupts a playing speed clip
mid-sentence, Pi-verified (commit `44a735c`); **buzzer earcon** (two-beep, Safety-only) baked
in front of the 3 safety WAVs; **re-delivery (CHANGE-only)** — a preempted speed CHANGE is
re-announced from **L2 current belief** when the channel frees (L4 `is_idle` + Arbiter `poll`),
51/51 unit checks, **committed `f4f5166` + pushed; Pi-working.** One all-angle tick remains (belief
60→80 mid-safety must announce 80) on a future Pi soak. Repo hygiene: stopped tracking models/audio
(commit `5401499`). Detail: `2026-06-20_session_report.md`.
**PRESENTATION DAY 2026-06-24:** GitHub reframe = "Cognitive Driver Assistance System"; repo slug →
`raspi-cognitive-adas`; README §1–5 locked (English, FPS = **~18**); ARCHITECTURE_VIEWER.html
restyled + all 11 boards made post-cutover-correct. **Safety-family precision MEASURED** (Ped
Warning 0.975 · Ped Crossing 0.978 · School Zone 0.972 @conf0.45 → suppression at one tier level,
no per-class split; provisional windows OK now). Thermal-governor scenario parked. No decision code
changed. Detail: `2026-06-24_session_report.md`.
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
**Safety re-ranked 2026-06-17 (Thai road-safety research):** **School Zone 30** · Pedestrian
Warning 25 · **Pedestrian Crossing 20 = threshold** (now the lowest safety rank) · Speed
CHANGE 12 · Curve Ahead 10 · Crossroad/Traffic Signal 8 · No Parking/U-turn/Stop/Passing 4
· Speed REMINDER 2. School zone = highest systemic risk (kids + sight-line occlusion). All 3
safety ≥ threshold (interrupt-capable); only numbers changed, structure untouched. **Speed
outputs share the channel and carry ranks too.**

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

The **L1–L4 belief-state speed-limit subsystem is the speed AUTHORITY — cutover DONE and
Pi-verified 2026-06-17**, including real audio output through the MAX98357A speaker.

- All four leaves + facade implemented, unit-tested, and Pi-verified; full CMake build
  passes; audio plays the subsystem's decisions on hardware.
- **Authority cutover COMPLETE (2026-06-17):** the legacy `CooldownManager` voter-input
  suppression and the `[CONFIRMED]`/`SpeedSignLifecycle` path are **removed**; L1/L2/L3 is
  the authority by default (`cfg.shadow=true`; `--shadow` is now a no-op). **Anti-spam is now
  owned entirely by L3** (CHANGE-always / REMINDER-on-fresh / SuppressContinuation).
- **Pi-verified working:** detection + alerts normal, and **K=2 now changes value on the 2nd
  confirm WITHOUT waiting out the old 5 s cooldown** — the coupling is gone.
- **Non-speed signs are silent** (no legacy `[CONFIRMED]` print, no audio) until Brain 2 —
  expected and on-direction.

What remains: **K=1 vs K=2 final choice** (now a free, measured tuning — both behave as
designed); decide via `Pending`/`age` telemetry. A4 housekeeping: drop the orphaned
`SpeedSignLifecycle.{h,cpp}` from CMake + delete the files.

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

- **HEAD `4693470`, CLEAN, in sync with `origin/fix-gil`.**
- 2026-06-17 commits (oldest→newest): `4cc4165` cutover · `50e994b` K=2 default · `0c49ec5`
  extract shared L4 · `2c06d59` MomentaryEngine+MomentaryPolicy · `4693470` Brain 2 wired +
  audio. (Earlier `d4b858a` = docs snapshot.)
- Brain 2 sources now in CMake: `MomentaryPolicy.cpp`, `MomentaryEngine.cpp`,
  `BehaviorPolicyRouter.cpp`, `MomentaryAudioMap.cpp`. 10 momentary WAVs in `assets/audio/`.
- **Orphaned `SpeedSignLifecycle.{h,cpp}` still compiled** (legacy `[LC-SHADOW]` baseline,
  now unused) — A4 housekeeping: delete files + CMake entry. Harmless for now.

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

### Cutover — remove voter-input suppression ✅ DONE + Pi-verified 2026-06-17
> Implemented in `main.cpp` + `Types.h` (−107/+27). Pi run: detection/alerts normal,
> K=2 changes value on the 2nd confirm with no 5 s wait, no audio spam. The plan below is
> kept as the record of what was changed.
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
- **K≥2 blocked by voter-input cooldown — ✅ RESOLVED by cutover 2026-06-17.** Root cause was
  one **shared** `TemporalVoter` fed cooldown-gated input: after the 1st confirm,
  `cooldown.activate()` zeroed that class's vote input for 5 s, so L2 never got the 2nd
  consecutive confirm K≥2 needs. Cutover removed the voter-input suppression (voter now fed
  raw `top_class`); Pi-verified K=2 changes value on the 2nd confirm with no 5 s wait. Anti-spam
  moved to L3. **K=1 vs K=2 is now a free, measured tuning choice — no longer a coupling.**
- **`rearm_after` vs cooldown interaction (R6)** — occlusion >600 ms during cooldown
  re-arms L1 → next confirm `fresh=TRUE`. Measure at bench; don't pre-tune.
- **Cutover invariants (R3–R5)** — `reset()` must not be wired to presence loss;
  `onValue` once per episode confirm (not per frame); `tick()` stays single-threaded.
- **Camera-only staleness** — no-forget belief can be wrong after a turn onto an unsigned
  road (no map/GPS). Accepted for v1; surfaced via age-as-display.

---

## Next Immediate Tasks

*Speed (PersistentState):* ✅ DONE — cutover + bench + K=2 decided + Pi-verified. (Optional:
A4 housekeeping = delete orphaned `SpeedSignLifecycle`.)

*Brain 2 (Momentary) — next:*
1. ✅ **Notification Arbiter DONE** (commit `e9c8419`) — both brains route through it.
2. ✅ **Refactor #3 DONE + committed `f4f5166` + pushed** — kill-aplay (`44a735c`) +
   completion-feedback (L4 `is_idle` poll) + **re-delivery (CHANGE-only)** from L2 current belief.
   Pi-working; one all-angle tick remains on a future Pi soak (belief 60→80 mid-safety → must
   announce 80).
3. ✅ **Safety-family precision MEASURED 2026-06-24** (Ped Warning 0.975 · Ped Crossing 0.978 ·
   School Zone 0.972 @conf0.45 → suppression at one tier level). **Bench-tune** `MomentaryPolicy`
   numbers + reminder/suppression cooldowns (test placeholders Safety 5s/Warning 15s/Restriction
   30s → user sets production; provisional values OK now).
4. **Presentation: ✅ PUBLISHED to public `main` 2026-06-25** (commit `7026136`, fast-forward push).
   Evolution-story README (16 sections; §2 Timeline real **v1.1→v1.7** + **v2.2**); v2.2 folder
   flattened/renamed → `version_2.2_cognitive_architecture/`; docs/ charts + README_assets +
   ARCHITECTURE_VIEWER + 3 benchmark docs (INT8_AB / FP32_ENVELOPE / ARCHITECTURE). **Method:**
   selective bring-over (NOT merge — `fix-gil` & `main` are **unrelated histories**, merge-base empty).
   **Internal kept OFF main** (live on `fix-gil` only): PROJECT_STATUS, session/EOD reports,
   architecture_issues, claude_engineering_growth_skill. **Safety:** `backup-main-before-v2` pushed;
   v1.1–1.7 history intact; merged `.gitignore` drops global `*.png/*.jpg` so presentation imgs track
   (models/audio/demo stay local). **✅ ALL LIVE:** repo **renamed → `raspi-cognitive-adas`** (local
   remote updated); **demo video WITH AUDIO** uploaded via GitHub web (main `fed5e0c`/`142bdf0`);
   **GitHub Pages ON** → viewer live `https://triphet-dm.github.io/raspi-cognitive-adas/ARCHITECTURE_VIEWER.html`;
   `.gitignore` synced on `fix-gil`. **Branch model:** `main` = public home (Pages serves it, public
   edits go here) · `fix-gil` = dev. **Optional polish only:** `evo_*` Timeline shots; DC-DC power §13;
   remove the stale `<!-- enable Pages -->` comment in README §5.

---

## Resume Point For Next Session

- **Read:** this file, then `2026-06-25_session_report.md` (benchmarks + README case-study +
  export-consistency A/B), then `2026-06-24_session_report.md` (presentation + safety precision),
  then `2026-06-17_session_report.md` (cutover + Brain 2 bring-up), then
  `2026-06-15_session_report.md` (Brain 2 architecture FROZEN), then
  `2026-06-14_session_report.md` (behavior laws + Q1–Q8). Older: `FP32_SPEED_ENVELOPE.md` /
  `INT8_AB_RESULTS.md` (06-16 speed perf), `2026-06-10_session_report.md` (speed L1–L4 detail).
- **Finished (Pi-verified, pushed `origin/fix-gil`):** speed cutover (L1–L4 = authority, L3
  anti-spam, K=2 default); shared L4 (#1); Brain 2 core (MomentaryEngine + MomentaryPolicy +
  BehaviorPolicyRouter); class→wav `MomentaryAudioMap` (#2); 10 momentary WAVs; **momentary
  speech audible on Pi**; safety re-ranked (School Zone 30 > Ped Warning 25 > Ped Crossing 20);
  **refactor #3 (Arbiter wiring + kill-aplay + buzzer + re-delivery) committed `f4f5166` + pushed.**
- **Next:** (a) **Presentation:** README §6–10 (Hardware/Power, 8 Laws, Build&Run, Limitations,
  Tech Stack); GitHub ops — rename repo → `raspi-cognitive-adas`, default branch `main`, enable
  Pages, build `docs/` (architecture.png, hardware.jpg, demo.gif from `demo/*.mp4`). (b) **Bench /
  Pi soak:** set production reminder/suppression + `MomentaryPolicy` numbers (precision now measured
  → provisional values OK); one remaining re-delivery all-angle tick (belief 60→80 mid-safety must
  announce 80); K=1 vs K=2 final measured choice.
- **Do NOT:** re-open `attention_rank`/threshold structure (only numbers tune); add an episode
  lifecycle to momentary; touch L1 re-arm for re-delivery (it's an L3-level re-announce); fix
  `MomentaryPolicy` numbers from assumption; wire `reset()` to presence loss; break the
  single-threaded `tick()`; add interfaces/inheritance/event-bus/DI/generics.
- **Decided already:** L1–L4 authority + L3 anti-spam; **K=2 default**; imgsz **512** locked;
  sync (no async); two-brain; Momentary timestamp model (no lifecycle); single `attention_rank`
  axis; INTERRUPT_THRESHOLD = lowest safety rank (= Ped Crossing after re-rank); shared generic
  L4 (`submit(filename)`); single-slot latest-wins; `aplay` v1.
