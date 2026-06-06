Project Status
==============

Latest Stable Branch:
fix-gil

Latest Commits:
a81155e docs: add 2026-06-06 session report
539657f fix(decision): store ROI per class
527a323 fix(camera): add per-slot mutex for double buffer
38e33d0 fix(camera): resolve async camera GIL deadlock

Completed:
- GIL deadlock fixed
- Double buffer race fixed
- ROI ownership fixed

Open Issues:
- classify_ms metrics
- TemporalVoter tie-break logic
- Additional field testing

Next Task:
- Road testing with multiple speed signs
- Validate cooldown behavior
- Validate 50→60→80 transition scenarios