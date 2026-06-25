# 2026-06-24 Session Report — GitHub Presentation + Safety Precision Measured

> **Bookkeeping note (filed 2026-06-25):** this report was reconstructed after the fact. The
> 06-20 session ended with the re-delivery code + docs *written but the doc TEXT left in a
> pre-commit state*; the commits/push and this 06-24 presentation work were never journaled.
> Source of truth used: git history, `demo/log.txt`, ARCHITECTURE_VIEWER.html diff, and the
> standing project memories. No code changed on 06-24 — this was a presentation / measurement /
> design day.

## Session Summary

A **presentation + measurement + design** day (no decision-engine code changed). Brain 1/2 were
already frozen, committed, and pushed (see "Correction" below). Work focused on making the project
**presentable on GitHub** and closing one standing open item (measured Safety precision).

1. **GitHub presentation kicked off** — project reframed as a **"Cognitive Driver Assistance
   System"**; repo slug decision (`raspi-cognitive-adas`); README §1–5 drafted + GPT-reviewed.
2. **ARCHITECTURE_VIEWER.html restyled + corrected** — new light theme/Inter font; board b1
   rebuilt; **all 11 boards scrubbed to the post-cutover pipeline** (legacy nodes relabeled
   "REMOVED at cutover 06-17", not deleted, to keep edges valid).
3. **Safety-family detector precision MEASURED** — closes the "bind suppression to *measured*
   precision" open item (was the last thing gating momentary suppression tuning).
4. **Thermal-governor robustness scenario** raised and **parked** (revisit at Brain 3 drowsiness).
5. **Demo media captured** — Pi run video + photos + `demo/log.txt` (a real on-Pi run that shows
   the speed pipeline + momentary `PLAY`/`PREEMPT` working).

---

## Correction to the 06-20 Record (the "forgot to save")

The 06-20 session report and PROJECT_STATUS were written **before** the final commits and were
never refreshed, so their text still described an uncommitted / unpushed state. Git is the truth:

- `f4f5166 feat(decision): CHANGE-only re-delivery (Arbiter poll + L2 re-derive)` — **the 8
  re-delivery files were committed** on 06-20 (exactly the set 06-20 listed as "uncommitted").
- `f7a7cee docs(eod): 2026-06-20 session report + PROJECT_STATUS update` — docs committed.
- Branch `fix-gil` is **`up to date with origin/fix-gil`** → **everything was pushed.**

So as of 06-25: refactor #3 (Arbiter wiring + kill-aplay + buzzer + **re-delivery**) is
**committed and pushed**, not "uncommitted/3-ahead". PROJECT_STATUS header corrected this session.

**Re-delivery / preemption field evidence (`demo/log.txt`, on-Pi):** belief transitions
(`sign_60` → CLS `sign_80` → `sign_90`), L3 anti-spam lines (`SUPPRESS-CONT`, `SUPPRESS-CD`),
momentary `[MOMENTARY] PLAY Pedestrian_crossing rank=20`, and **`[MOMENTARY] PREEMPT School_Zone
rank=30 F336`** — i.e. a rank-30 safety sign preempting the channel, kill-aplay path live on Pi.
(The log captures normal driving; it is not a scripted 60→80-mid-safety re-delivery proof — that
specific all-angle case remains the one item to tick off on a future Pi soak. Code is committed.)

---

## Work Completed

### 1. GitHub presentation (ACTIVE — was parked)
Repo: `https://github.com/Triphet-DM/traffic-sign-edge-ai` (to be renamed).
- **Framing locked:** "Cognitive Driver Assistance System" (deliberately NOT generic "ADAS" — avoids
  lane-keeping / sensor-fusion expectations). Official long name kept: *"High Performance Embedded
  DualVision Traffic Sign and Driver Monitoring."* "DualVision" = two cameras (sign + driver
  monitoring), **not** stereo — clarifier line to add.
- **Repo slug decided:** rename `traffic-sign-edge-ai` → **`raspi-cognitive-adas`**.
- **README language = English**; FPS quoted as **~18** everywhere (real 18.5 — do not let it slip
  to "~19").
- **README §1–5 LOCKED** (drafted + GPT-reviewed): 1 Title+tagline+intro · 2 Key Features (7
  bullets) · 3 Demo + Hardware spec · 4 Architecture + interactive-viewer link · 5 Results (model
  perf / engineering findings / system features). §5 bench facts: packing OFF → 12–13 FPS (packing
  critical); threads=3 only +1 FPS → threads=2 to reserve cores for Engine B.
- **README §6–10 REMAINING:** 6 Hardware & Power (DC-DC) · 7 Behavioral Design Principles (8 Laws)
  · 8 Build & Run (flags) · 9 Limitations & Future Work (drowsiness / thermal / GPS) · 10 Tech
  Stack + "models/audio not in repo" note.

### 2. ARCHITECTURE_VIEWER.html — restyle + pipeline correctness (committed this session)
- **Restyle:** default light/white theme, **Inter** font (was Comic Sans), crisp edges, vivid CAT
  palette.
- **Board b1 rebuilt:** retitled "Cognitive Driver Assistance Architecture"; Brain 1/2 → "Decision
  Engine A/B"; 2-stage emphasis (YOLO = region / CLS = value); +3 boxes (Engineering Challenges,
  Performance Snapshot ~18 FPS, Deployment On-Device); attention-rank fixed (School 30 > Ped Cross
  20 = threshold).
- **All 11 boards now post-cutover-correct:** legacy (CooldownManager / `[CONFIRMED]` /
  SpeedSignLifecycle / "LEGACY/SHADOW authority") **relabeled** "REMOVED at cutover 06-17" rather
  than deleted (keeps edges valid). Fixed K=1→K=2 (b6, b10), non-preempt → preemptible kill-aplay
  (b2, b8, glossary), async → "optional, sync default" (b0, b2); glossary + b10 click-detail panels
  fixed. `node --check` passes.
- ⚠️ **Caveat:** b10's live-animation **sequence JS** was NOT rewritten — when played it may still
  step through the old flow visually. Review together when it can be watched.

### 3. Safety-family precision — MEASURED (closes a freeze open-item)
Detector = deployed YOLO (yolo11n family); eval via ultralytics `val` on the held-out roboflow
**test split** (`D:\Project Version yolo 13 512`, 1300 imgs / 15 classes, balanced).
- **Per-class P @ conf=0.45 iou=0.45 (production conf):** Pedestrian_Warning 0.975 ·
  Pedestrian_crossing 0.978 · School_Zone 0.972. Uniform + high → **set suppression at ONE tier
  level, no per-class split** (no weak class). Whole-set P 0.98 / R 0.965; mAP50 ~0.98–0.99.
- **Trust (NOT 100%, by design):** conf objection CLOSED (P holds at production 0.45 → well
  calibrated); still **in-distribution** (= upper bound); static images can't exercise the K=2
  voter. Extra evidence: a senior's real road video ran accurately on PC (same model) — attacks the
  static + in-distribution objections; residual PC↔Pi gap is camera/frame-cadence, not compute.
- **Conclusion (leaning option B):** evidence strong enough to set **provisional** suppression
  windows now (don't block on road clips); hold the most aggressive values until on-Pi real-camera
  soak. Detector is not the cry-wolf bottleneck in good conditions. Current placeholders: Safety 5s
  / Warning 15s / Restriction 30s (`MomentaryPolicy.cpp`).

### 4. Thermal governor — robustness scenario raised, PARKED
Pi 5 in a parked sun-soaked cabin → throttle / self-shutdown risk. Parked as a design idea;
revisit at Brain 3 (drowsiness). User's plan: startup cooldown before allowing sign detection;
priority **driver (drowsiness) > signs** (Law 2 + Law 8) — load-shed signs while cooling, keep
drowsiness running. Engineering input to apply when un-parked: **hysteresis (dual threshold) +
min-dwell + moving-average temp** to stop flapping; keep model loaded + gate inference (don't tear
down); **graceful degradation** (19→10→5 FPS) over binary on/off; small Thermal-Governor state
machine (NORMAL → COOLING → CRITICAL). Three questions to answer first: (1) is the board powered
when parked/engine-off? (ACC-off may cut power → real case = hot cold-start) (2) active cooling? (a
fanless board floors at cabin ambient ~55–60 °C) (3) Brain 3 doesn't exist yet.

### 5. Demo media captured (local only)
`demo/` = Pi run video (`*.mp4`, ~5.9 MB) + 3 photos + `demo/log.txt`. The log is a genuine on-Pi
run at sync/512/threads=2 (~18 FPS, ~48 ms infer) showing the full pipeline + momentary
PLAY/PREEMPT. **Kept local / gitignored** (binary-hygiene rule, like models + audio); the README
plan is to convert `demo/*.mp4` → `docs/demo.gif` (3–5 s) and use a photo as `docs/hardware.jpg`.

---

## Build / Validation
No build or unit-test changes this session (no decision code touched). On-Pi behavior evidence is
`demo/log.txt` (pipeline + momentary working; safety PREEMPT observed). Safety precision numbers
from ultralytics `val` on the test split (host/PC).

## Performance
Operating point unchanged: sync, imgsz 512, threads=2, **~18 FPS** / ~48 ms infer, CPU 50–60%.
(Note the canonical figure is **~18**, not ~19 — use ~18 in all presentation copy.)

---

## Open Items / Next Session
- **README §6–10** remain to draft (Hardware/Power, 8 Laws, Build&Run, Limitations, Tech Stack).
- **GitHub operational tasks (user, on GitHub):** (1) rename repo → `raspi-cognitive-adas` then
  `git remote set-url origin …/raspi-cognitive-adas.git` (2) default branch → `main` (3) enable
  **GitHub Pages** (root) → viewer live at `…github.io/raspi-cognitive-adas/ARCHITECTURE_VIEWER.html`
  (4) create `docs/`: `architecture.png`, `hardware.jpg` (demo photo), `demo.gif` from `demo/*.mp4`.
- **b10 live-animation JS** sequence still old — review when watchable.
- **Folder-flatten** of the repo tree still pending (clean tree only — mind CMake paths).
- **Bench / Pi soak (decision side, unchanged from 06-20):** set production reminder/suppression +
  `MomentaryPolicy` numbers (now backed by measured precision → provisional values OK); the one
  remaining re-delivery all-angle tick (belief 60→80 mid-safety must announce 80) on a Pi soak;
  K=1 vs K=2 final measured choice.
- **A4 housekeeping:** orphaned `SpeedSignLifecycle.{h,cpp}` still compiled in CMake (dead).

## Do NOT
- Quote FPS as "~19" in any presentation copy — it is **~18**.
- Delete legacy nodes from the viewer (relabel "REMOVED at cutover" to keep edges valid).
- Un-park thermal-governor / forget+GPS / momentary re-delivery without bench.
- Re-open attention-rank/threshold structure (numbers only); add an episode lifecycle to momentary;
  touch L1 re-arm for re-delivery; break single-threaded `tick()` / Arbiter.
