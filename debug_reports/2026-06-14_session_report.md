# 2026-06-14 Session Report — Behavior-First Design Meeting (Non-Speed Signs)

## Session Summary

Design "meeting" (Diamond + Claude + GPT). **No coding.** Goal: design how the **10
remaining non-speed sign classes** should *behave* in the real world, before deriving any
architecture. The session produced a **project reframe**, a **behavioral grouping
hypothesis**, **8 locked behavioral laws**, and **behavior decisions for 8 scenarios
(Q1–Q8)**.

> Format note: ran as an engineering review, GPT acting as a hard reviewer ("ไม่อวย").
> The final grouping, laws, and Q-answers below incorporate GPT's 4 corrections + the
> added Law 8 from the closing review.

The speed subsystem (L1–L4) was **not touched** and remains the completed
**PersistentState** family.

---

## The Big Outcome — Project Reframe

The project changed its own definition:

> from **"AI Traffic Sign Detection"** (detect sign → announce sign)
> to **"Cognitive Driver Assistance"** (detect sign → decide whether the driver should be
> interrupted → only then speak).

The system does **not** drive and does **not** make driving decisions. Its job is
**attention redirection**: the machine speaks, the human observes, the human makes the
final decision (**human-in-the-loop**). The system's central question shifted from
*"can I detect more signs?"* to ***"which information deserves the driver's attention
right now?"***

---

## The 15 YOLO Classes (ground truth, from `YoloDetector.h`)

Speed (done, L1–L4): `sign_50 sign_60 sign_80 sign_90 sign_100`
Remaining (designed today): `Pedestrian_Warning_Sign Pedestrian_crossing School_Zone
curve_ahead sign_four_way Traffic_sign no_parking no_passing no_stop no_u_turn`

Clarified this session: **`Traffic_sign` = a static *warning* sign meaning "traffic
light ahead"** — NOT a live signal that changes color. So there is no real-time
red/yellow/green detection in scope.

---

## Current Behavioral Grouping — WORKING HYPOTHESIS (not final)

Grouped by **behavior pattern**, not by sign meaning. The grouping is a **working
hypothesis** — it can still change once each class is tested on real road footage.

| Family | Members | Stack | Notes |
|---|---|---|---|
| **PersistentState** (Speed) — *done* | sign_50/60/80/90/100 | L1+L2+L3+L4 (has belief) | unchanged |
| **Safety** (human life) | Pedestrian_Warning, Pedestrian_crossing, **School_Zone** | momentary (no L2) | School_Zone provisional — see below |
| **Warning** (road hazard) | curve_ahead, sign_four_way, **Traffic_sign** | momentary | |
| **Restriction** (prohibition) | no_parking, no_passing, no_stop, no_u_turn | momentary | |

**Why not "FINAL":** `School_Zone` is provisionally **Safety / momentary** (because
camera-only has no end-of-zone sign and no GPS, so a true zone-state can't be honestly
maintained). But if bench footage shows the right behavior is a **pseudo-persistent
zone** (e.g. continuous reminder within a distance), this grouping must change. Do not
design as if it is locked.

**Key reframe of the grouping itself:** the 3 momentary families share the *same
mechanism*; they differ only in **policy** (priority, cooldown, suppression, interrupt) —
they are NOT 9 separate subsystems and NOT one monolith. "Family" is the unit of
isolation (per-sign = too many; one lump = unrepairable). This is the **Pattern Family**
idea (GPT) = the same place Claude's "shared engine + per-class policy table" was heading.

---

## The 8 Behavioral Laws (LOCKED)

1. **You don't have to say everything.** Reporting every sign ⇒ the driver stops
   listening (effective signs reported → 0).
2. **Life-safety signs override everything.** Human safety > communication consistency.
   A half-spoken lower-priority message is disposable; safety must never be late. *Only
   the Safety tier may interrupt.*
3. **One episode = one notification.** A sign visible for 4 s is announced once.
   Prolonged visibility is not a reason to repeat.
4. **No queue anywhere.** Stale information is dropped, never stored-and-replayed. When
   the mouth is free, **re-evaluate from current state**, don't replay the past.
5. **Truth validation belongs exclusively to the Perception layer.** The Behavior layer
   does **not** re-validate perception truth. *(Explicitly NOT "perception is 100%
   correct" — perception still has false positives and the dataset/model keep improving;
   it just means trust-filtering is not the Behavior layer's job, because it has no
   better information than perception.)*
6. **The driver's intake is limited.** Therefore the system should usually speak only one
   thing at a time.
7. **Reliability is everything (anti cry-wolf).** Frequent false alarms train the driver
   to ignore the system, which destroys the attention-redirection premise.
8. **Machine attention capacity is intentionally limited.** *"The machine must behave as
   if its own attention capacity is limited."* It does not think "I detected 8 signs, I
   will report 8 signs"; it thinks "I can focus on one important thing now — which one
   matters most?" This law is the generator of suppression, arbitration, cooldown,
   interrupt, and stale-rejection.

---

## Behavior Decisions (Q1–Q8 + Meta)

| Q | Decision |
|---|---|
| **Q1** Safety override | **Interrupt immediately** (Safety tier only). Human safety overrides communication consistency. The interrupted message is disposable. Buzzer (option C) deferred. |
| **Q2** Perception vs Behavior | **Separate cleanly.** Perception decides *"is it real?"*; Behavior decides *"is it worth speaking now?"* Behavior does attention/value filtering, NOT truth filtering. |
| **Q3** Tier ≠ identity | **Speak again.** Pedestrian_Warning ≠ Pedestrian_crossing even though same tier. Tier is for priority + default suppression only; cooldown binds to the sign's *meaning* (per-class), not to the tier. |
| **Q4** Repeated exposure | **One episode = one notification.** Long visibility never triggers a repeat. |
| **Q5** Overload at intersection | **Intentionally suppress.** Speak the single highest-priority sign, drop the rest. Maximize comprehension, not delivery. The driver sees the prohibition signs anyway. |
| **Q6** Distance vs Precision | **Optimize net safety benefit — balance experimentally.** *(Corrected from Claude's "reliability first", which was too absolute.)* Early-but-untrusted warning gives ~0 reaction benefit (cry-wolf); but a slightly-later high-precision warning isn't automatically better either. e.g. 85% @ 30 m may beat 95% @ 5 m. Tune the announce-distance/threshold by experiment, do not fix a global rule from assumption. |
| **Q7** Attention cooldown | **Always monitor; mouth rests, eyes never close.** Keep perceiving + updating state while silent so the next utterance is current. Safety pierces the cooldown. |
| **Q8** Deferred notification | **No defer (no queue). Drop, then re-derive from current state when free.** Persistent info (speed) is still-true and will be re-derived by the next natural cycle; momentary info goes stale instantly and dies. "Postpone" is the wrong model; "re-evaluate from current reality" is right. |
| **Meta** | **B — maximize useful information the driver can safely process, not the number of signs reported.** The bottleneck is the human's attention budget, not the machine's detection count. B is the generator of every answer above. |

---

## Vocabulary Locked This Session

Cognitive Driver Assistance · Attention Redirection · Human-in-the-loop ·
**Suppression Strength** · **Cry-Wolf Effect** · Perception(truth) vs Behavior(usefulness)
· Pattern Family · One-episode-one-notification · No-queue / re-derive-from-state.

Refinement proposed (not yet locked): **Suppression Strength = { K_confirm (how many
frames to confirm before speaking), cooldown (silence window after speaking) }** — one
axis controlling two knobs, NOT two independent columns (in the draft table "cooldown"
and "suppression" were the same axis named twice).

---

## Still Open — must resolve before architecture freeze

1. **Suppression must be bound to *measured* precision, not importance alone** (Laws 7 +
   Q6). "Safety = low suppression" is only safe if Safety-class precision is high enough.
   ⇒ pre-bench task: **measure per-class precision for the Safety family.**
2. **Same-family / same-tier tie-break** (e.g. no_parking + no_u_turn in one frame).
   Proposed: nearest/largest ROI wins, or fall back to latest-wins. **Not locked.**
3. **Suppression = {K_confirm, cooldown}** framing — proposed, **not confirmed.**
4. **cooldown ↔ L1 re-arm interaction (R6)** must be set *per family*, not free-floating.
5. **Cross-class arbiter is stateful** (must know what is currently playing to preempt).
   This is an architectural consequence of Law 2 — defer to the architecture phase.
6. **School_Zone** momentary-vs-pseudo-persistent-zone — provisional, revisit at bench.

---

## Intentionally NOT decided yet (parked)

buzzer interrupt · global cooldown 2–3 s · per-class confidence threshold · second-stage
model (e.g. RF-DETR) · exact suppression parameter values · exact family implementation.

---

## Next Session — Behavior Simulation Round 2 (NOT architecture yet)

Architecture design was judged **premature** — philosophy was only just locked today and
not enough edge cases are explored. Next session continues **behavior-first** with new
scenarios:

- **Scenario A:** car at 80 km/h enters a `School_Zone` with **no end-of-zone sign** —
  how should the system behave? (this is the momentary-vs-zone decision made concrete)
- **Scenario B:** `Pedestrian_crossing` detected, disappears for 200 ms, reappears 500 ms
  later — **same episode or new episode?** (re-arm timing, real)
- **Scenario C:** heavy traffic, 5 signs in a row, system suppresses 4, driver misses a
  real prohibition — **acceptable or not?** (stress-test Law 1 / Law 5 / Meta=B)

Only after Behavior Simulation Round 2 do we move to designing `SignTypeRouter` +
`Priority Arbiter`.

---

## What Was NOT Done

- No code. No architecture/class/thread design. No parameter tuning. Speed L1–L4
  untouched.

---

## Resume Point For Next Session

- **Read:** `PROJECT_STATUS.md`, then this report.
- **Finished today:** project reframe (Cognitive Driver Assistance); 8 behavioral laws;
  behavioral grouping hypothesis (4 families); Q1–Q8 behavior decisions; vocabulary.
- **Next:** Behavior Simulation Round 2 (Scenarios A/B/C above) — still behavior-first,
  **not** architecture. Then design Router + Arbiter.
- **Do NOT:** treat the grouping as final; design as if perception is perfect; set
  suppression from importance without precision data; build Router/Arbiter or any code
  before behavior simulation round 2; decide the parked items.
