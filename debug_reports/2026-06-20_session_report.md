# 2026-06-20 Session Report — Refactor #3 Complete (kill-aplay + buzzer + re-delivery)

## Session Summary

A heavy **implementation + design-review** day that closed out **refactor #3** (the last
remaining Brain-2 integration work). Three audio-path features landed and a fourth was
implemented:

1. **Notification Arbiter wired end-to-end** — both brains (speed pipeline + momentary) now
   route through the cross-brain Arbiter instead of direct latest-wins to the shared L4.
2. **kill-aplay** — `NotificationManager` now spawns `aplay` via `posix_spawn` and can kill it
   mid-clip, so a safety sign genuinely **interrupts** a playing speed clip. **Pi-verified.**
3. **Buzzer earcon before safety** — a two-beep "warning" tone baked into the 3 safety WAVs so
   a preempted speech clip + the safety clip don't run together confusingly.
4. **Re-delivery (CHANGE-only)** — a speed CHANGE cut by a safety preempt is re-announced once
   the channel frees, reading the **current L2 belief** (never a stale stored value).
   Code complete, 51/51 Arbiter unit checks, **working on Pi but not yet fully tested / not
   committed.**

Three commits pushed-pending on `fix-gil` plus uncommitted re-delivery. Also did repo hygiene
(stopped tracking models/audio) and a 3-way design review (with GPT) on each piece.

---

## Work Completed (chronological)

1. **Resume:** read `debug_reports/`; surfaced yesterday's open decision (re-delivery scope).
2. **Repo hygiene — commit `5401499`:** stopped tracking model (`*.ncnn.bin/.param`) + audio
   (`*.wav`) — `git rm --cached` 24 files + `.gitignore` rules (later `*.exe` too). User can
   retrain models / re-record audio, so they're kept local only. GitHub repo ~20 MB, no size
   pressure — this is hygiene (binary recommits bloat `.git` forever).
3. **Decision — re-delivery scope = CHANGE-only** (user confirmed Claude's recommendation):
   a preempted REMINDER does NOT re-deliver (re-derivable + low miss-cost). Momentary
   re-delivery (curve/no_passing miss-cost idea, raised by user + GPT) **PARKED** — good idea,
   revisit after bench; keep confirmed scope first.
4. **Arbiter wiring — commit `e9c8419`:** speed (`pipeline.tick`) + momentary (`run_decision`)
   both `arbiter.submit(rank, file, now)`; play only on Play/Preempt; interim direct-submit
   replaced. Pure logic, 35/35 host tests at that point.
5. **kill-aplay — commit `44a735c`:** design-reviewed with GPT then implemented + Pi-verified.
6. **Audio re-record:** user re-recorded all 20 WAVs. Caught a botched export (only 2 unique
   contents across 20 files + 37.83 s each); user fixed → 20 unique, 1.10–1.97 s, 44.1k mono.
7. **Buzzer:** decided always-before-safety, baked into the 3 safety WAVs with `sox`
   (two-beep square 2 kHz). Volume too low at first (`gain -6`) → re-baked louder with `norm`.
8. **Re-delivery:** implemented A+B+C (see Fixes), 51/51 Arbiter tests, Pi-working.

---

## Problems Discovered

### kill-aplay root cause — `std::system` discards the child PID
`play_blocking` used `std::system("aplay …")` which blocks the audio thread and gives **no
handle** to the child → latest-wins could only replace the *queued* slot, never the *playing*
clip. To interrupt, L4 must own the `aplay` process.

### Stale-CHANGE risk in re-delivery (user-found "รอยรั่ว")
If "CHANGE→60" is preempted and the belief moves 60→80 while the safety clip plays (and that
new CHANGE→80 is dropped because the channel is busy), replaying the **stored** "60" would be
wrong. Resolution: re-delivery stores no value — it re-derives from **L2's current belief** at
replay time (Law 4). User initially proposed a K=2 re-validation gate; refined to "read the
belief L2 already maintains — don't wait for a fresh confirm" (a passed sign may never re-appear).

### Botched audio re-record
First re-record produced 20 files with only 2 distinct md5s and 37.83 s each (export error).
User re-did it; verified 20 unique md5 / 1.10–1.97 s before proceeding.

---

## Fixes / Features Implemented

### 1. Stop tracking models + audio — commit `5401499`
`git rm --cached` 24 binaries; `.gitignore` += `*.ncnn.bin`, `*.ncnn.param`, `*.wav`, `*.exe`.
Local copies untouched; a fresh clone is no longer self-contained (accepted — user retrains /
re-records). Old copies remain in pre-`5401499` history (not worth a rewrite).

### 2. Notification Arbiter wiring — commit `e9c8419`
Both brains route through `NotificationArbiter::submit` → Play→`nm.submit`, Preempt→(then)
`nm.preempt`. `busy_until_` scaffold for the busy window. 35/35 host unit checks.

### 3. kill-aplay — commit `44a735c` (Pi-verified)
`NotificationManager`: `std::system` → `posix_spawn` (holds the `aplay` PID). New
`preempt(file)` = **SIGTERM → 100 ms → SIGKILL**, kill issued by the producer (decision
thread); the **audio thread is the sole `waitpid` reaper**. `child_pid_` guarded by mutex;
`waitpid(WNOHANG)` + clear are atomic under the same mutex as the kill → **closes PID-reuse**
(a zombie's PID isn't recycled until reaped). No `TERMINATING` state (storm already bounded by
the Arbiter's strict-rank gate + mutex + SIGTERM idempotency). Speed pipeline + momentary path
route Arbiter Preempt → `preempt()`, Play → `submit()`. **Pi-verified: safety cuts the speed
clip mid-sentence; no zombies.**

### 4. Buzzer earcon (baked, not GPIO)
Chose in-band WAV through the single audio channel over a GPIO active buzzer (keeps the
single-audio-channel architecture; zero new code; atomic with the clip). Two-beep square 2 kHz,
`norm`-leveled, ~0.42 s, baked in front of the 3 safety WAVs via `sox` (clean originals kept in
`_safety_orig/`). Buzzer is **exclusive to the Safety tier** (Law 7 anti cry-wolf).

### 5. Re-delivery (CHANGE-only) — IMPLEMENTED, Pi-working, **uncommitted**
- **A — L4 `is_idle()`** (`NotificationManager`): true iff no clip playing AND no pending —
  closes the submit→spawn gap. (Option-3 "ask L4" beat the event-mailbox idea: reuses
  kill-aplay's existing `child_pid_` state; nothing new to build.)
- **B — Arbiter**: `submit(…, bool redeliver_eligible=false)`; on Preempt, if the clip being
  cut was eligible → set `redeliver_owed_`. New `poll(now)` syncs the channel to idle and
  returns/clears the owed flag. `current_eligible_` tracks the holder. 51/51 host checks.
- **C — pipeline `redeliver(now)`**: reads `l2_.current()` → CHANGE for that value → routes
  through the Arbiter (eligible=true, so a re-delivery can itself be re-delivered). `[SHADOW][L3]
  REDELIVER CHANGE` log under `--shadow-verbose`.
- **main**: per frame, `if (notifier.is_idle() && arbiter.poll(now)) pipeline.redeliver(now)`.
- Only speed CHANGE passes `eligible=true`; REMINDER + momentary = false.

---

## Validation / Build / Runtime

- **Host unit tests (g++ -Wall -Wextra):** NotificationArbiter **51/51** (35 base + 16
  re-delivery). NotificationManager / pipeline / main build on **Pi only** (posix_spawn, ncnn).
- **Pi-verified:** Arbiter wiring (`e9c8419`), kill-aplay (`44a735c`), buzzer audible,
  re-delivery audible (speed returns after safety). **Not yet tested all-angles** (see Resume).
- **Audio assets:** 20 WAVs, 20 unique md5, 1.10–1.97 s, 44.1 kHz mono 16-bit; 3 safety files
  now carry the baked buzzer (~+0.42 s); clean originals in `assets/audio/_safety_orig/`.

## Performance Benchmarks
None today. Operating point unchanged: sync, imgsz 512, threads=2, ~19 FPS / ~48 ms infer,
CPU 50-60%. Decision logic (L1–L4, Brain 2, Arbiter) runs in microseconds.

---

## Architecture Discussions / Decisions

### Approved (3-way review with GPT)
- **Re-delivery = CHANGE-only**, re-derived from **L2 current belief** (not stored value);
  the **Arbiter holds** the re-delivery flag; timing via **L4 `is_idle` poll** (option-3,
  reuse existing state — no event mailbox).
- **kill-aplay:** SIGTERM→100 ms→SIGKILL · **`posix_spawn`** (multithread-safe > fork's
  COW/lock-hazard) · producer kills / audio-thread sole reaper · mutex + `child_pid_` guard
  (no `TERMINATING` state) · reap+clear atomic under the kill mutex (closes PID-reuse).
- **L4 API:** two named methods `submit` / `preempt` (rejected GPT's command-object — the
  L4 verb set is closed at two; future states were already designed away).
- **Buzzer:** always-before-safety, **baked WAV through the speaker** (not GPIO), Safety-only.

### Interim / deviation (flagged)
- **Stamp-at-decision, not stamp-at-completion** for re-delivery cooldown — deviates from the
  locked decision but the gap is ~clip (≈2 s) vs the 180 s cooldown = negligible, **and the
  cooldown values are test placeholders** (user sets production minutes later). Revisit only if
  production tuning shows it matters; would reuse the same `is_idle` transition + an L3 stamp
  method.
- **`busy_until_` scaffold** now overridden by the real `poll()`/`is_idle` idle signal — left
  as a harmless fallback.

### Parked (not now)
- **Momentary re-delivery** (extend to high-miss-cost momentary like curve_ahead / no_passing
  via a `DeliveryPolicy { NEVER_REPLAY, REPLAY_IF_INTERRUPTED }` field) — revisit after bench.
- Time-based forget + GPS; strict stamp-on-completion.

---

## Known Issues / Limitations
- Re-delivery **not fully tested** (esp. belief moving 60→80 mid-safety → must say "80").
- Re-delivery **not committed** (8 files in the working tree).
- `MomentaryPolicy` numbers + reminder/suppression cooldowns are **test placeholders** — user
  to set production minutes; tune at bench. K=1 vs K=2 still a free measured choice.
- Per-class precision of the Safety family still unmeasured.
- **A4 housekeeping:** orphaned `SpeedSignLifecycle.{h,cpp}` still compiled in CMake (dead).

---

## Current Working-Tree Status
**Branch `fix-gil`, 3 commits ahead of `origin/fix-gil` (unpushed):**
- `5401499` chore: stop tracking model + audio assets (kept local)
- `e9c8419` feat(decision): route both brains through NotificationArbiter
- `44a735c` feat(audio): preempt-kill aplay via posix_spawn for safety interrupt

**Uncommitted (re-delivery, 8 files):** `NotificationArbiter.{h,cpp}`,
`NotificationManager.{h,cpp}`, `ShadowSpeedLimitPipeline.{h,cpp}`, `main.cpp`,
`tests/NotificationArbiter_test.cpp`. Re-recorded WAVs + baked buzzer are local only
(gitignored).

---

## Resume Point For Next Session
- **Finished:** refactor #3 code complete — Arbiter wiring + kill-aplay (Pi-verified, committed)
  + buzzer (baked) + re-delivery (Pi-working, uncommitted). Re-delivery scope = CHANGE-only,
  re-derive from L2 current belief, Arbiter-owned, L4 `is_idle` poll.
- **Do first:** finish all-angle Pi testing of re-delivery — **key case: belief 60→80 mid-safety
  → re-delivery must announce 80, not 60** (`[SHADOW][L3] REDELIVER CHANGE belief=…`); also
  safety-over-safety then re-deliver; check no zombies (`ps`), buzzer level OK. Then **commit**
  re-delivery; consider `git push` (drops the old model/audio blobs from GitHub).
- **Then (bench / user-driven):** set production reminder/suppression cooldowns + `MomentaryPolicy`
  numbers; K=1 vs K=2; measure Safety-family precision.
- **Do NOT:** silently keep stamp-at-decision if production tuning needs precision (it's flagged);
  re-open the attention-rank/threshold structure; un-park momentary re-delivery / forget+GPS
  without bench; add a `TERMINATING` state or command-object; break Arbiter single-thread.
- **Optional housekeeping:** A4 — delete orphaned `SpeedSignLifecycle.{h,cpp}`.
