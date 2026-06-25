# 2026-06-25 Session Report — Doc Reconciliation + Full Benchmark Capture + README Case-Study Redesign

## Session Summary

A **documentation, measurement-capture, and presentation-engineering** day (no decision-engine
code changed). Three threads: (1) reconciled the project docs with git truth after discovering
the 06-20 reports were left in a pre-commit state; (2) captured the full cross-backend benchmark
suite + resolved the post-export validation question with a clean export-isolation A/B; (3)
rebuilt `README.md` from scratch into an **Embedded AI Systems Engineering case study**. Also did
a connector-spacing experiment on the architecture viewer that was rolled back.

---

## Work Completed

1. **Found the "forgot to save" gap.** git showed re-delivery (`f4f5166`) + docs (`f7a7cee`) were
   actually committed **and pushed** on 06-20, but the report/PROJECT_STATUS *text* still read
   "uncommitted / 3-ahead / HEAD 44a735c". Reconciled both to git truth.
2. **Journaled the unrecorded 06-24 presentation day** (`2026-06-24_session_report.md`): GitHub
   reframe, ARCHITECTURE_VIEWER restyle, Safety precision. Committed earlier.
3. **ARCHITECTURE_VIEWER connector spacing** — attempted, over-corrected, **rolled back** (below).
4. **Discovered the README prose was never saved** — only the section outline survived in memory.
   Rebuilt `README.md` as a real file, then iteratively redesigned it per two GPT reviews.
5. **Captured the full benchmark suite** (numbers below) — previously only partially in
   `INT8_AB_RESULTS.md`; now preserved here + in README.
6. **Resolved the post-export validation question** with an export-isolation A/B (below).
7. **docs/ vs demo/ reorg** + memory updates.

---

## Problems Discovered + Root Cause

### Stale docs (the "forgot to save")
06-20 reports were written **before** the final commits and never refreshed. Root cause: report
authored first, code/docs committed after, text not re-touched. Same failure class the user hit
with re-delivery. Fix: reconcile to git, and note it explicitly in the 06-24 report.

### README prose lost
When "saved" was said on 06-24, only the **outline + decisions** went to memory/report — no
`README.md` file was ever written. The full drafted prose existed only in chat. Root cause: "save"
conflated journaling with producing the artifact. Fix: rebuilt the file from the saved outline;
flagged that this is the lesson (journal ≠ artifact).

### Architecture-viewer connectors — over-correction (rolled back)
First complaint: arrows anchored flush to block edges (cramped). Attempted fix added an
`offsetOut()` that pushed **both** endpoints out along the side normal (clamped to 30% of span).
Result: arrows **floated** — the arrowhead no longer touched the target block (e.g. Router→Momentary
ended mid-air). Root cause: symmetric offset created a gap at the *target* too, not just the source.
**This is hand-rolled orthogonal routing (not auto-layout / Excalidraw),** so the float was purely
the added offset. **Decision: full rollback** — endpoints sit ON the block boundary again
(`p1=sidePoint(a,fs), p2=sidePoint(b,ts)`). Spacing-via-block-separation deferred ("ชั่งมันก่อน").
Viewer now byte-matches the committed version (no diff).

---

## Investigations / Benchmarks (PRESERVE)

All detection benchmarks use the **same YOLO11n model at imgsz 512**; only platform/backend/config
changes. Camera runtime 960×560.

### Desktop — NVIDIA RTX 4070 Super (GPU)
| Format | FPS |
|---|---|
| best.pt (PyTorch) | 97–110 |
| best.onnx (ONNX Runtime) | 145–170 |
ONNX > PyTorch under **GPU acceleration** (CUDA EP / graph optimization & kernel fusion) — an
inference-engine difference, **not** CPU framework overhead.

### Raspberry Pi 5 — ONNX Runtime
| Threads | FPS | Infer |
|---|---|---|
| 1 | 4.6 | 208 ms |
| 2 | 7.9 | 119 ms |
| 3 | 9.8 | 93 ms |
| 4 | 10.0 | 89 ms |
**Thread saturation after 3** (3→4 ≈ no gain).

### Raspberry Pi 5 — NCNN (production backend)
Production config: **fp32 compute · `use_fp16_storage` ON · `use_fp16_packed` ON.** fp16 here =
memory-format/bandwidth optimization, **NOT** fp16 arithmetic (compute stays fp32).
| Config | FPS | Infer |
|---|---|---|
| fp32 + packing ON | **18.3** | 47.8 ms |
| fp32 + packing OFF | 13.8 | 66.9 ms |
| int8 | ~1–3 FPS slower | 51.2 ms |
| fp16 arithmetic | no speed gain | accuracy ↓ |
NCNN thread scaling: 1→12.9 · 2→**18.0** · 3→19.7 · 4→20.2 FPS.

### Backend characterization
- **NCNN ≫ ONNX on ARM:** NCNN @ 2 threads (18 FPS) beats ONNX @ 4 threads (10 FPS) → ~**1.8×**
  with fewer cores.
- **Packing critical:** OFF = 13.8 vs ON = 18.3 FPS (**−32%**).
- **threads=2 chosen deliberately** — reserve the marginal core for the planned driver-monitoring
  model (systems decision, not benchmark-chasing).

### Low-precision investigation (negative results)
- **int8 detects fine on Pi but is SLOWER** (51.2 vs 47.8 ms). A76 fp32+fp16 NEON already
  efficient; quant/dequant overhead > int8 compute savings. int8 beats fp32 **only when fp16 is
  force-OFF** (not the deploy path). Earlier Windows "0 TP" = x86 int8-kernel artifact; ARM Pi
  detects fine (Pi = ground truth). int8 pays off **only with an NPU** (Hailo/Coral) — calib
  assets kept.
- **fp16 arithmetic = no-op** — no speed gain + accuracy drop. The win is fp16 **storage/packed**
  (bandwidth), a different opt from fp16 arithmetic.

---

## Model Evaluation Consistency — export-isolation A/B (PRESERVE)

Resolved the open "NCNN vs .pt" question by running both on the **same test split at deployment
thresholds** (`conf=0.45 iou=0.45`):

| Evaluation (test split @ conf 0.45) | mAP50 | Precision | Recall | mAP50-95 |
|---|---|---|---|---|
| best.pt (PyTorch) | 0.982 | 0.980 | 0.965 | 0.907 |
| **NCNN export (deployed, md5-identical)** | **0.980** | **0.977** | **0.966** | **0.906** |
| Δ (export effect) | −0.002 | −0.003 | +0.001 | −0.001 |
| Roboflow validation *(reference only)* | 0.991 | 0.985 | 0.984 | — |

**Δ ≤ 0.3% on every metric** across PyTorch→NCNN, same split + same conf → genuine **post-export,
deployment-equivalent** verification (the NCNN binary tested is md5-identical to the Pi one).
Roboflow row = **training-platform reference only** (different split=val, default conf → NOT a
like-for-like delta). Caveat: all splits in-distribution (one Roboflow collection) → upper bound;
final validation = on-Pi real-camera soak. NCNN val command used:
`yolo val model=...\runs\detect\train\weights\best_ncnn_model data=...\data.yaml imgsz=512 split=test conf=0.45 iou=0.45`.

---

## Dataset (PRESERVE)
**13,039 labeled images** (usable/annotated count; unusable captures excluded — earlier "13,482"
included rejects, corrected 2026-06-25) · 15 classes · ~884–962 per class (balanced) · test split
1300 imgs / 1398 boxes · Roboflow. (Class count **= 15** confirmed by the val output — resolves the
earlier "15 vs 16"; 16 was the export output-attr count, not classes.)

## Hardware (updated)
Pi 5 (8 GB) · **HQ Camera IMX477 + 16 mm telephoto lens** · Picamera2 960×560 · MAX98357A I²S ·
3 W speaker · **Official Active Cooling Fan**. (Active cooling now present — relevant to the parked
thermal-governor assumptions.) Power/DC-DC intentionally unspecified (not finalized).

---

## Architecture Discussions / Decisions

### Approved this session
- **README narrative = embedded systems engineering, NOT an ML/YOLO showcase.** Architecture +
  behavioural design are the project identity → placed high; benchmarking / backend / low-precision
  investigation are the engineering star.
- **Results framing = deployment consistency** (post-export, deployment-equivalent), not "99%
  model." Evaluation metrics limited to mAP50 + safety-family precision + the export-isolation
  table; whole-set P/R **not** shown as a showcase (it appears only inside the export A/B).
- **Keep fp32 + fp16 storage/packing** on Pi 5 CPU. Do NOT enable fp16 arithmetic (no-op + acc
  loss) or int8 (slower on CPU). Pursue speed via imgsz or an accelerator.
- **demo/ = local working folder (gitignored `demo/*`); docs/ = curated presentation assets.**
  Untracked `demo/log.txt` (was committed); raw stills/video stay local; `docs/demo.jpg` committed.
- **README structure mods (from GPT review):** Behavioral Design moved up after Architecture;
  "Quantization & Precision Study" → **"Low-Precision Inference Investigation"**; added a **Design
  Evolution Timeline** section (scaffold).

### Rejected / superseded
- **Connector endpoint offset** (floating arrows) — rolled back; endpoints must touch the block.
- **fp16 arithmetic / int8 on Pi CPU** — measured slower/no-gain; only an NPU flips this.
- **Driver camera in the Hardware section** — moved to Future Work (don't imply dual-camera exists).
- **Roboflow-vs-Local as a clean export delta** — rejected (different split+conf); demoted to
  reference. Replaced by the best.pt-vs-NCNN same-condition A/B.

---

## Validation / Build
No code build this session (docs/measurement only). Benchmark numbers above are from the user's
Pi runs + dev-box `yolo val`. Architecture viewer JS: `node --check` passed after the
offset rollback; file now identical to committed (no diff).

---

## Known Issues / Limitations
- **README uncommitted** + author has not done a final manual read.
- **Design Evolution Timeline = RESOLVED** — `origin/main` already had the real history. README §2
  rewritten as a two-phase **evolution story**: Phase 1 **v1.1→v1.7** (ONNX→NCNN→RGB/BGR fix→
  temporal voting, real milestones + links to `version_1.x_*` folders) + Phase 2 **v2.2** (cognitive
  redesign, framed as continuation NOT a separate project). `README_assets/` (7 imgs: 5 real
  detection screenshots + 2 hardware) imported from `origin/main` and wired (hero = `sign_90.png`).
- **PUBLISH/branch strategy (open):** new README + v2.2 on `fix-gil`; `origin/main` (default, public)
  still shows the OLD README + `version_1.1–1.7` folders + `README_assets`. Need to land the new
  README + v2.2 on `main` **without** deleting the v1.x history. Merge direction TBD (recommend a
  merge that keeps main's v1.x folders — they're untouched by `fix-gil`, so a merge preserves them).
- docs/ images **produced 06-25** (hardware.jpg, architecture.png, class_dist.png,
  training_curves.png). Still pending: **demo video WITH AUDIO** — the system speaks, so a silent
  GIF drops the point; upload `demo/Video.mp4` via the GitHub web editor / a Release for an inline
  player with sound (do NOT convert to gif). Timeline `evo_*` shots still needed.
- Architecture-viewer **connector spacing** still un-improved (rolled back; spacing-via-block-move
  parked). b10 live-animation JS sequence still old.
- Decision-side (unchanged from 06-20): production `MomentaryPolicy`/cooldown numbers; on-Pi soak;
  the re-delivery all-angle tick (belief 60→80 mid-safety → must announce 80); A4 dead-file cleanup.

---

## Deployment / Publish to GitHub (the big event of the day) — ✅ DONE & LIVE

- **Topology discovery:** `fix-gil` (dev) and `origin/main` (public: old README + `version_1.1–1.7` +
  `README_assets/`) are **UNRELATED histories** (`git merge-base` empty). Plain `git merge` would fail;
  `--allow-unrelated-histories` would conflict on README/.gitignore. **Chosen: selective bring-over.**
- **Backup first:** `git branch backup-main-before-v2 origin/main && git push origin backup-main-before-v2`
  → snapshot of pre-v2 public main (`d053242`). Restore if needed: `git push origin backup-main-before-v2:main --force`.
- **v2.2 flattened + renamed:** `version_2.2_cls_roi_debug/raspi_project_v11/` → top-level
  `version_2.2_cognitive_architecture/` (src/ directly inside, matching the `version_1.x_*` convention).
  README build path + Timeline links updated; 54 git renames (history preserved).
- **Selective bring-over** (`git checkout -b main origin/main; git checkout fix-gil -- <public paths>`):
  - **Public (commit `7026136`, pushed):** README.md, docs/, version_2.2_cognitive_architecture/,
    ARCHITECTURE_VIEWER.html, merged .gitignore, README_assets/ (identical), + **3 benchmark docs on
    purpose:** INT8_AB_RESULTS.md, FP32_SPEED_ENVELOPE.md, ARCHITECTURE.md (engineering evidence).
  - **Internal (on `fix-gil` only, NOT on main):** PROJECT_STATUS.md, session/EOD reports,
    architecture_issues_v2.2.txt, claude_engineering_growth_skill/.
- **Merged `.gitignore`:** dropped main's global `*.png`/`*.jpg` (would have blocked presentation imgs)
  while keeping build/model/audio/demo ignores. Synced to `fix-gil` too.
- **Repo renamed** `traffic-sign-edge-ai` → **`raspi-cognitive-adas`** (user, GitHub UI); local remote
  updated. **Demo video WITH AUDIO** uploaded by user via GitHub web (main → `fed5e0c`, `142bdf0`).
  **GitHub Pages enabled** → viewer **LIVE**: `https://triphet-dm.github.io/raspi-cognitive-adas/ARCHITECTURE_VIEWER.html`.

### Branch model going forward (IMPORTANT for next session)
- **`main` = public home** (GitHub Pages serves it). Public edits (README, ARCHITECTURE_VIEWER.html,
  docs/) → commit + push to `main` → Pages auto-rebuilds (~1 min). Can also edit on github.com web.
- **`fix-gil` = dev branch** (v2.2 code + internal docs). The two branches are unrelated histories —
  **treat `main` as canonical for public files** to avoid divergence (README + viewer exist on both).
- **Safety nets on origin:** `backup-main-before-v2` (old main), `fix-gil` (full dev history).

---

## Current Working-Tree Status
**Everything committed + pushed — nothing uncommitted (EOD verified).**
- **`fix-gil`** (dev): clean, in sync with `origin/fix-gil`. Last commits: `be17d3b` README redesign ·
  `d35a3e2` flatten/rename v2.2 · `5aaaf08` record publish · `ed89633` sync .gitignore.
- **`main`** (public): `7026136` v2.2+README bring-over, then user web edits `fed5e0c`, `142bdf0`
  (demo video). Live on GitHub Pages.
- **`backup-main-before-v2`** on origin → `d053242` (pre-v2 main snapshot).
- Memory updated: `raspi-github-presentation-parked`, `raspi-safety-precision-measured`.
- **No untracked files outside `.gitignore`.** models/audio/`demo/`/`*.exe` intentionally local.

---

## Resume Point For Next Session

- **Finished:** doc reconciliation; 06-24 + 06-25 reports; full benchmark capture; export-isolation
  A/B (Δ≤0.3%); README case-study redesign; **PUBLISHED to public `main` + renamed to
  `raspi-cognitive-adas` + demo video (audio) + GitHub Pages LIVE** (see Deployment section);
  fp16/int8 closed; class count = 15; dataset = 13,039.
- **Optional polish remaining (NOT blockers — repo is complete & live):** (1) `evo_*` old
  screenshots for §2 Timeline. (2) DC-DC power spec §13 (intentionally blank). (3) viewer connector
  spacing (rolled back; parked). (4) clean the `<!-- TODO: enable GitHub Pages -->` comment in
  README §5 (Pages now on) — public-file edits go on `main` now.
- **Then (decision side, unchanged):** set production `MomentaryPolicy`/reminder/suppression
  numbers (precision measured → provisional OK); on-Pi soak; re-delivery 60→80 all-angle tick;
  K=1 vs K=2; A4 delete orphaned `SpeedSignLifecycle`.
- **Do NOT:** quote FPS as ~19 (it's ~18); enable fp16 arithmetic or int8 on the Pi CPU; re-add the
  connector endpoint offset (arrows must touch blocks); imply a driver camera exists now; present
  Roboflow vs Local as a clean export delta; commit raw `demo/*` (use `docs/`); invent the Timeline
  milestones; re-open attention-rank structure; break single-threaded `tick()`/Arbiter.
- **Decided already:** README = embedded-systems-engineering narrative; results = deployment
  consistency; fp32 + fp16 storage (no arithmetic/int8 on CPU); demo/=local, docs/=presentation;
  Behavioral Design high; section renamed to "Low-Precision Inference Investigation".
