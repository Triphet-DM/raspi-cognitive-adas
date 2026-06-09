# REPORT WORKFLOW
End of Day Workflow

At the end of a work session, the following commands may be used:

EOD

or

End of Day Report

When one of these commands is issued, perform all tasks below automatically.

Task 1 — Create Daily Session Report

Create a new report:

debug_reports/YYYY-MM-DD_session_report.md

The report must include:

Session summary
Work completed
Problems discovered
Root cause analysis
Investigations performed
Fixes implemented
Validation results
Build status
Runtime test results
Performance benchmarks
Architecture discussions
Approved decisions
Rejected decisions
Superseded designs
Known issues
Current working-tree status
Next-session starting point

Requirements:

Record both committed and uncommitted work.
Record reasoning, not just outcomes.
Preserve benchmark numbers.
Preserve architectural decisions.
Do not omit failed experiments.

The report should serve as a historical engineering record.

Task 2 — Update PROJECT_STATUS.md

Update:

debug_reports/PROJECT_STATUS.md

This file represents the current state of the project.

It should always contain:

Current Status

High-level overview of project progress.

Stable / Verified

Features, fixes, and configurations verified to work.

Working Tree

Current uncommitted work.

Architecture Decisions

Important architectural choices currently in effect.

Approved Direction

Designs approved for future implementation.

Rejected / Superseded Designs

Designs intentionally abandoned and why.

Current Performance Baseline

Latest verified benchmark numbers.

Known Issues

Open issues and limitations.

Next Immediate Tasks

Highest-priority development tasks.

Resume Point For Next Session

Where development should continue next.

Requirements:

Reflect the latest project state.
Replace outdated information.
Keep only currently relevant information.
Treat this file as the project's source of truth.
Task 3 — Resume Information

Every session report and PROJECT_STATUS.md must contain:

# Resume Point For Next Session

This section must clearly answer:

What is finished?
What is still in progress?
What should be done next?
What should NOT be done?
What architectural decisions have already been made?

A future session should be able to resume work by reading only:

PROJECT_STATUS.md
Latest session report
Rules
Preserve technical reasoning.
Preserve benchmark results.
Preserve architecture decisions.
Preserve rejected ideas.
Preserve known limitations.
Record both committed and uncommitted work.
Never lose context needed for future development.
Optimize for long-term project continuity.
Treat reports as engineering documentation, not chat summaries.
Recommended Startup Workflow

At the beginning of a new session:

Read PROJECT_STATUS.md
Read the latest session report
Review current working-tree changes
Continue from the Resume Point For Next Session section

This should be sufficient to restore project context without reviewing previous chat history.