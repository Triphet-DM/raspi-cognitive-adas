# PROJECT STATUS

> Source of truth / high-level dashboard for raspi_project v11.
> Overwritten whenever a major architectural decision changes.
> Detailed history lives in the dated session reports in `debug_reports/`.

**Last updated:** 2026-06-10
**Branch:** `fix-gil`
**HEAD:** `72bd460 feat(audio): complete L1-L4 shadow pipeline with speaker integration`
**Remote:** pushed to `origin/fix-gil` (in sync).

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

1. **Bench validation** (printed signs + speaker): 50-stuck, flicker, gapless 50→60,
   re-arm, reminder. Diff `[SHADOW][L3]` vs `[CONFIRMED]`. Lower `--shadow-reminder-sec`
   for the run so REMINDER is observable.
2. **Telemetry read**: `Pending` counts → K=1 vs K=2; `age` distribution → age-display.
3. **Authority cutover readiness** evaluation (honor invariants R3–R5 below).
4. After cutover: remove old `SpeedSignLifecycle` + its `[LC-SHADOW]` call.

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

## Current Performance Baseline

| Config | FPS | infer |
|---|---|---|
| **Threads = 2** (default) | ~10 | ~93 ms |
| Threads = 3 (old) | ~8 | ~116 ms |

Inference-bound. FP16 arithmetic and Vulkan give no gain. Audio runs on a dedicated
thread (playback outside the lock) and is expected to be FPS-neutral — **to be confirmed
by measurement at the bench.**

---

## Known Issues

- **Repeat-confirmation bug** — fixed by construction in L1–L4; **bench proof pending.**
- **`classify_ms` (Bug #3, LOW)** — diagnosed as rolling-average dilution (only the
  sparse confirm frame is nonzero), **not** a classifier skip. Metric still misleading.
- **TemporalVoter tie-break (Bug #4, LOW)** — alphabetical, not recency. Keep orthogonal.
- **`Pending`+`fresh`→`REMINDER` of old value** — K≥2 only; K=1 default avoids it. Parked.
- **`rearm_after` vs cooldown interaction (R6)** — occlusion >600 ms during cooldown
  re-arms L1 → next confirm `fresh=TRUE`. Measure at bench; don't pre-tune.
- **Cutover invariants (R3–R5)** — `reset()` must not be wired to presence loss;
  `onValue` once per episode confirm (not per frame); `tick()` stays single-threaded.
- **Camera-only staleness** — no-forget belief can be wrong after a turn onto an unsigned
  road (no map/GPS). Accepted for v1; surfaced via age-as-display.

---

## Next Immediate Tasks

1. End-to-end architecture/data-flow review (one picture, Camera→…→Speaker).
2. Run the 5 bench validation scenarios; capture logs + audio behavior.
3. Measure `Pending`/`age`; decide K and age-display threshold.
4. Decide authority cutover readiness.

---

## Resume Point For Next Session

- **Read:** this file, then `2026-06-10_session_report.md`.
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
