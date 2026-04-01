---
description: "Run a multi-perspective code review (Speed, Security, Usability, Conservative) on specified files or a recent change. Use when: code review, review PR, review change, audit code."
argument-hint: "Describe the target to review (files, change, or subsystem)"
agent: "agent"
---

Perform a multi-perspective code review of the specified target using the
review-multi-perspective skill.

Read the skill file at `.github/skills/review-multi-perspective/SKILL.md` and
follow its full procedure: identify the review target, spawn four parallel
review subagents (Speed, Security, Usability, Conservative), evaluate and
deduplicate their findings, synthesize a prioritized remediation plan, and
present it for user approval before implementing anything.
