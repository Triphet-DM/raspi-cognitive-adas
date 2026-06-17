# 2026-06-15 Session Report — Attention Arbitration & Architecture Freeze (Non-Speed)

## Session Summary

Design "meeting" (Diamond + Claude + GPT). **No coding.** Continued the behavior-first
design from 2026-06-14 and pushed it through to an **architecture freeze** for the
non-speed families. The session produced: the **two-brain split made concrete**, the
**BehaviorPolicyRouter / Notification Arbiter** layers, the collapse of `priority` +
`interrupt_level` into a single **`attention_rank` + `INTERRUPT_THRESHOLD`**, and a full
**cross-engine attention ranking** that includes the speed outputs.

> Format note: ran as a hard engineering review (GPT "ไม่อวย"). Two GPT challenges were
> raised this session; one was accepted (speed must join the ranking), one was **withdrawn
> by GPT** after rebuttal (`can_preempt` boolean → kept the threshold instead). The freeze
> below is the post-rebuttal state both reviewers signed off on.

Speed L1–L4 (PersistentState) is **unchanged in behavior**, but the freeze identifies
**three integration refactors** that will touch its boundary at build time (see §7).

---

## The Big Outcome — Architecture Phase Complete

The non-speed behavior architecture is now **FROZEN**. The only remaining open item is
**numeric tuning of the attention scale**, which is a **bench-phase** task, not an
architecture-phase task. Philosophy → behavior → architecture is closed; the next move
is measurement, not more design debate.

The reframe deepened: this is not a traffic-sign detector. It is a
**real-time attention scheduler on edge hardware** where the contended resource is the
**driver's attention channel** (the "mouth" / one audio notification at a time), not CPU.

---

## 1. Two-Brain Architecture (LOCKED)

The system has two decision engines that feed **one shared output**.

| Brain | Owns | Signs | Internals |
|---|---|---|---|
| **Brain 1 — PersistentState Engine** (DONE) | belief state that persists after the sign is gone | speed_50/60/80/90/100 | L1 → L2 → L3 (→ shared L4) |
| **Brain 2 — Momentary Engine** (NEW, designed) | transient info, no persistent state | all 10 non-speed signs | timestamp suppression → interrupt check → emit action (→ shared L4) |

**Key asymmetry (this is the reason the two brains differ — LOCKED):**
- **Momentary information has no second chance.** Drive past it and the information dies
  forever. ⇒ it must be able to **interrupt** to claim the channel in time.
- **Persistent state can be re-derived.** Miss a speed announcement and the belief is
  still true; the next natural cycle re-derives and announces it (Law 4 / Q8). ⇒ speed
  does **not** need interrupt rights — it just waits for the mouth to free.

This is why momentary signs earn interrupt authority that persistent (speed) does not.

---

## 2. BehaviorPolicyRouter (NEW — renamed from "SignTypeRouter")

Sits right after `TemporalVoter` confirm. Generalizes the old `is_speed()` gate.

Renamed because "Router" understated it: it is not packet-forwarding, it is
**behavioral classification** — it answers *"what behavioral policy should this sign
follow?"* (→ which brain), not *"which engine owns this packet?"*

---

## 3. Momentary Engine — no episode lifecycle (LOCKED)

The 2026-06-14 report still spoke of "episode lock / re-arm" for momentary signs. **That
is now gone.** The "Episode" abstraction was the wrong model for momentary signs.

Replaced by a **per-class notification timestamp** (the *Human Memory Suppression
Model*):

```
detection → TemporalVoter (Detection Stability: K/N frames)
          → Notification Suppression Window:  now − last_notified[class] ≥ window[class] ?
                NO  → SUPPRESS (drop, silent)
                YES → check interrupt eligibility → emit Action → last_notified[class] = now
```

**Why this dissolves Scenario B (the freeze-blocker from last session):** a sign occluded
for 0.5 s then re-detected is *trivially* inside any reasonable suppression window, so it
is suppressed automatically. "Same episode vs new episode" **never has to be answered** —
the timestamp subsumes it. No re-arm logic, no L1/L2/L3 for momentary signs.

### Notification Completeness Principle (LOCKED)
> Once useful information has been successfully delivered to the driver, subsequent
> detections must not trigger immediate re-announcement until sufficient temporal
> separation has occurred.

Driver memory matters more than detector state. The model is **human-centric**
("does the driver already know?"), not machine-centric ("is this the same sign?").

### Class memory, not physical instance (LOCKED — and forced, not chosen)
Suppression binds to the **class** (`last_notified[class]`), not to a physical sign
instance. This is **forced** by scope: camera-only + no tracking + no GPS ⇒ physical
instance identity is **not observable**. Distinguishing "same sign reappearing" from "a
different sign of the same class" would require a spatial tracking/identity layer =
rejected scope. Happily it also coincides with the human-centric answer (a 2nd identical
warning within the memory window adds ≈0 value and risks cry-wolf).
*Residual risk:* a long community zone with crossings every ~100 m is announced once →
acceptable for v1; the fix is **shorter per-class window for the Safety tier** (policy
table), NOT instance tracking.

---

## 4. Attention Arbitration — `attention_rank` (LOCKED)

### What was rejected this session
- **`interrupt_flag` (boolean)** — insufficient: cannot express safety-on-safety ordering
  (School Zone vs Pedestrian Warning vs Pedestrian Crossing arriving near-simultaneously).
- **separate `priority` + `interrupt_level`** — they semantically overlap, and two free
  fields allow **selection order ≠ interrupt order** = logical incoherence.
- **`can_preempt` (boolean, GPT's alternative)** — *withdrawn by GPT.* It decouples
  "may interrupt" from rank, which re-introduces exactly the non-monotonic incoherence we
  had just eliminated. The claimed extensibility benefit is illusory (see below).
- **shared safety cooldown** — would let one safety sign suppress another safety sign;
  violates Law 2. (Note: per-class windows already prevent this by construction — keying
  on `last_notified[class]` means School Zone's window never touches Pedestrian Crossing.)

### What was locked
One field per announce-able output, plus one global constant:

```cpp
MomentaryPolicy {
  suppression_window,   // per-class, TIME
  attention_rank        // single axis: collapses priority + interrupt_level
}
// global:
INTERRUPT_THRESHOLD     // = Safety Boundary (see below)
```

**Arbiter rules:**
```
SELECTION (speaker idle):      pick highest attention_rank
PREEMPTION (speaker active):   interrupt current iff
                                   incoming.rank > current.rank
                                   AND incoming.rank ≥ INTERRUPT_THRESHOLD
```

Using one axis **forces** selection-ordering and interrupt-ordering to agree, killing the
incoherence class by construction.

### INTERRUPT_THRESHOLD = Safety Boundary (LOCKED — the elegant part)
The threshold is **not a magic number**. It is defined as **the lowest rank belonging to
the life-safety category** (i.e. School Zone). Therefore:

> "May interrupt active speech" ⟺ "is a life-safety sign."

This **re-derives Law 2** ("human safety overrides communication continuity") directly
out of the ranking. Adding a future class (e.g. Railroad Crossing) asks a *meaningful*
question — "is it life-safety?" — and places its rank above or below the boundary
accordingly. Combined with a **sparse scale** (gaps between ranks), inserting a new class
costs one number and touches nothing else — so the threshold is *not* rigid.

### Interruption is costlier than selection (LOCKED rationale)
Selection (mouth idle) is free; interruption (cutting a clip mid-sentence) has a fixed
cost (the driver hears a broken sentence). Interrupt therefore requires a **higher bar**
than selection — which is exactly what the threshold encodes (interrupt iff value exceeds
a floor).

---

## 5. Full Attention Ranking — includes the speed engine (LOCKED structure, provisional numbers)

Speed outputs share the same `L4` / speaker, so they **compete on the same channel** and
must carry a rank too (GPT's accepted Challenge 2).

| Output | attention_rank | Notes |
|---|---:|---|
| Pedestrian Crossing | 30 | immediate human-crossing risk NOW |
| Pedestrian Warning | 25 | crossing likely ahead soon |
| **School Zone** | **20** | **= INTERRUPT_THRESHOLD (Safety Boundary)** |
| Speed CHANGE | 12 | below threshold → selected over restrictions but cannot cut speech |
| Curve Ahead | 10 | low environmental redundancy, but below boundary |
| Crossroad / Traffic Signal | 8 | environmental redundancy helps the driver |
| No Parking / No U-turn / No Stop / No Passing | 4 | restrictions, aggressive suppression |
| Speed REMINDER | 2 | least urgent (re-stating known info) |

**Numbers are provisional — tune at bench (Q6: never fix road-dependent params from
assumption).** What is frozen is the *structure*: one scale, threshold at the Safety
boundary, speed outputs included, sparse spacing.

Worked consequence: Pedestrian Crossing **may** cut a playing "เปลี่ยนเป็น 80" (Law 2);
Speed CHANGE **may not** cut a playing "ห้ามจอด" (12 < 20) — and that is fine, because the
speed belief is still true and re-derives when the mouth frees.

---

## 6. Three temporal mechanisms — renamed, different natures (LOCKED naming)

All three were previously called "cooldown," which was confusing. They are distinct:

| New name | Old name | Layer | Unit | Scope |
|---|---|---|---|---|
| **Detection Stability Window** | K_confirm (TemporalVoter) | Detection | **frames (K/N count)** | whole system |
| **Persistent Reminder Interval** | 180 s reminder cooldown (L3) | Announce | seconds | speed only |
| **Notification Suppression Window** | class cooldown | Announce | seconds | momentary, per-class |

⚠️ **Detection Stability is a frame COUNT, not a timer** — do not implement it in seconds.
The legacy `CooldownManager` (5 s, per-class, wired into voter input) is unrelated and is
**removed at speed cutover**.

---

## 7. Three integration refactors that touch the "done" speed engine (TO-BUILD)

The shared output forces changes at the speed engine's boundary. Flagged now so they are
*known* refactors, not surprises:

1. **Extract L4 / NotificationManager out of `ShadowSpeedLimitPipeline`.** Both brains
   must feed **one shared L4** beneath the Arbiter; the speed pipeline currently owns its
   own L4 — that ownership moves down.
2. **Two decision→wav maps.** `SpeedAudioMap` (value→wav) stays speed-specific; the
   momentary engine needs its own `class→wav` map. L4 itself stays generic (plays a
   filename, single-slot latest-wins).
3. **Preemption = kill the playing `aplay` mid-clip.** Current audio is `play_blocking`
   outside the lock; latest-wins only replaces the *queued* slot, never the *playing*
   clip. Law 2 interrupt requires killing the running `aplay` subprocess — a genuinely
   new capability.

---

## 7b. Delivery Completeness Model (refinement — pending Arbiter)

Discovered during Track A bench review (Scenario 4). Started as "Attention Context Reset"
(re-arm speed when a safety event redirects attention) — **withdrawn** in favour of a
cleaner trigger. GPT + Claude converged on the reframe below.

**Core correction:** `re-arm (L1)` ≠ `re-announce (L3)`. L1 only tracks presence / episode
boundary; the announce/suppress decision is **entirely L3's** (same-value-after-gap is
gated by the reminder cooldown — `AnnouncementPolicy.cpp` rows 2/3). So Scenario 4's
observed "no repeat on temporary disappearance" is **already correct behavior**, driven by
the L3 cooldown gate, *not* by re-arm timeout. **Do not touch L1 re-arm.**

**The real trigger is delivery completeness, not attention.** This is *not a new law* — it
is the correct reading of the frozen **Notification Completeness Principle**: suppression
applies only to information that was **successfully delivered = the clip played to the
end**. A clip killed mid-sentence by a preempt was **NOT** successfully delivered ⇒
suppression must not apply ⇒ it is eligible for re-delivery.

```
Speed 50 playing → Pedestrian Crossing preempts (kill-aplay mid-clip)
    → delivery marked INCOMPLETE → after the safety clip ends, re-deliver "Speed 50" in full
Speed 50 played to completion → Pedestrian Crossing arrives later
    → delivery COMPLETE → NO re-delivery (driver already heard it)
```

**Falls out for free:** the "only signs above INTERRUPT_THRESHOLD reset speed" rule needs
no special case — only above-threshold signs can preempt (cut a clip), so only they can
cause an incomplete delivery. No_parking (rank 4) never cuts speech ⇒ never triggers
re-delivery. One mechanism, not two.

**Architecture cost (NOT free):** requires a new feedback edge **Notification Arbiter →
engine** — "this clip was preempted, not completed." The new capability is therefore not
just *interrupt audio* but **interrupt awareness** (the system must know which clip failed
to finish). This pairs with the kill-aplay to-build (§7.3); the Arbiter already knows when
it killed a clip.

**Status:** depends on the Arbiter, which is not built yet ⇒ **cannot implement now**.
Logged as a pending refinement. **Does not block speed cutover** (Track A) — current speed
behavior is correct as-is; this only adds re-delivery once the Momentary engine + Arbiter
exist. Rename locked: *Delivery Completeness Model*, not *Attention Context Reset*.

---

## 8. Vocabulary Locked This Session

Attention Arbitration · Driver Attention Channel (the contended resource) · `attention_rank`
· INTERRUPT_THRESHOLD = Safety Boundary · Notification Completeness Principle · Human
Memory Suppression Model · BehaviorPolicyRouter · Notification Arbiter · Detection
Stability Window / Persistent Reminder Interval / Notification Suppression Window ·
Two-Brain (Persistent vs Momentary) · momentary-has-no-second-chance asymmetry ·
**Delivery Completeness Model** (re-deliver only on preempted/incomplete delivery) ·
**interrupt awareness** (Arbiter knows which clip failed to finish).

---

## 9. Still Open (bench-phase, NOT architecture)

1. **Exact numbers in the attention scale** — tune at bench (Q6).
2. **Where exactly Speed CHANGE / REMINDER sit** relative to restrictions — structure
   locked, exact ranks provisional.
3. **Per-class suppression-window durations** (esp. short window for Safety tier).
4. **Per-class precision of the Safety family** (still the pre-bench gate from 06-14:
   "low suppression" is only safe if measured precision is high enough).

---

## 10. What Was NOT Done

- No code. No implementation. No parameter values fixed. Speed L1–L4 behavior untouched
  (only its future integration boundary documented).

---

## Resume Point For Next Session

- **Read:** `PROJECT_STATUS.md`, then this report, then `2026-06-14_session_report.md`.
- **Frozen this session:** two-brain split; BehaviorPolicyRouter; Momentary timestamp
  model (no episode lifecycle); `attention_rank` single axis; INTERRUPT_THRESHOLD = Safety
  Boundary (re-derives Law 2); full cross-engine ranking (structure); 3 renamed temporal
  mechanisms; 3 integration refactors (extract L4, 2 wav maps, kill-aplay).
- **Architecture Phase = COMPLETE.** Next is **measurement**, not design: bench-tune the
  attention scale + suppression windows, and measure Safety-family precision.
- **Do NOT:** re-open `priority`/`interrupt_level`/`can_preempt` (collapsed to
  `attention_rank` — settled); add episode lifecycle to momentary signs; fix scale numbers
  from assumption; implement before bench precision data exists; build any of this before
  the speed cutover decision is also resolved.
