# Technical Review Framework

## Review Goals

Review for correctness, realtime behavior, debuggability, deployment practicality, and learning value.

Prioritize:

1. Bugs and behavioral regressions.
2. Data ownership and thread safety.
3. Latency and memory risks.
4. Measurement correctness.
5. Maintainability and simplicity.
6. Testability and observability.

## Architecture Review Checklist

For every pipeline architecture, identify:

- Thread model.
- Stage responsibilities.
- Data ownership.
- Blocking points.
- Queue or slot semantics.
- Overwrite/drop behavior.
- Latency accumulation points.
- Shutdown/lifecycle behavior.
- Debug outputs.
- Metrics and timing semantics.

## Realtime Review Checklist

Ask:

- Is throughput separated from latency?
- Is queue size bounded?
- Can stale frames accumulate?
- Can result handling lag behind detection?
- Does logging distort timing?
- Does image saving distort timing?
- Are frame copies intentional?
- Are thread boundaries necessary?
- Is the architecture still debuggable on Raspberry Pi 5?

## Computer Vision Review Checklist

Ask:

- Is camera format known and logged?
- Is RGB/BGR conversion explicit?
- Is letterbox scale-back correct?
- Is model output format decoded correctly?
- Are boxes in the same coordinate system as the frame?
- Is ROI clamped before crop?
- Is classifier input saved for inspection?
- Is class order consistent with training?
- Is normalization consistent with export/training?

## Code Review Output

When reviewing code, lead with findings:

- Severity.
- File and line.
- Problem.
- Why it matters.
- Suggested fix or experiment.

Then include:

- Open questions.
- Test gaps.
- Summary.

If no major issues are found, say so clearly and mention remaining risks.

## Incremental Change Review

Before implementing, ask:

- What behavior must be preserved?
- What is the smallest useful change?
- What metrics should be compared before and after?
- What new failure modes does the change introduce?
- How can the user roll back or isolate the change?

