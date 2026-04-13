---
name: review-multi-perspective
description: "Multi-perspective code review using four parallel constructive reviewers (conservative, security, usability, speed) followed by a sequential adversarial gap-analysis pass. Each reviewer uses a two-phase inventory-then-assess approach for systematic coverage. Findings are synthesized, deduplicated, verified, and presented as a prioritized remediation plan."
argument-hint: "Describe the review target: file paths, modules, or scope"
---

# Multi-Perspective Code Review

## When to Use

- Reviewing code changes before merging
- Auditing a module, file, or subsystem for issues
- Evaluating a new implementation for correctness and quality
- Performing a thorough review of security-critical code
- Any situation where the user asks for a code review

## Overview

This skill runs four independent constructive reviewer subagents in parallel,
each applying a distinct lens with a two-phase inventory-then-assess approach
for systematic coverage. Their findings are synthesized and then passed to a
sequential adversarial reviewer that performs gap analysis on what the
constructive reviewers missed. The combined findings are deduplicated, verified
where possible, and presented as a single prioritized remediation plan. Large
review targets are chunked into segments to ensure every section receives full
attention.

## Reviewer Perspectives

| Reviewer | Focus |
|---|---|
| **Conservative** (`conservative-lens`) | Unnecessary new abstractions, changes that could have reused existing infrastructure, public API surface growth, backwards compatibility risks, ripple effects on downstream WF specs, over-engineering |
| **Security** (`security-lens`) | Unbounded recursion/iteration, missing validation at boundaries, unsafe node access across rewrite boundaries, missing Error nodes for failure paths, overly broad symbol visibility, inadequate fuzz coverage, regex safety |
| **Usability** (`usability-lens`) | Unclear naming, passes doing multiple things, imprecise WF specs, inconsistent patterns, poor error messages, hard-to-follow control flow, missing or misleading documentation |
| **Speed** (`speed-lens`) | Unnecessary allocations, redundant traversals, suboptimal pattern dispatch, pass count bloat, missing `dir::once`, cache-unfriendly access patterns, algorithmic complexity issues |
| **Adversarial** (`adversarial-lens`) | Hidden failure modes, wrong assumptions, untested preconditions, scope traps, gaps the other four reviewers share. **Runs sequentially after the constructive reviewers.** |

## Procedure

### Step 1 — Establish the Review Target

Determine what is being reviewed. This could be:
- Specific file(s) or line ranges
- A module or subsystem
- A diff or set of changes
- A specific implementation concern described by the user

Read all target files and relevant surrounding context (callers, tests,
WF specs, token definitions) before launching reviewers.

### Step 2 — Gather Context

Before spawning reviewers, collect:
- The full content of the files under review
- Relevant WF specs and token definitions
- Related tests
- Any pass pipeline context if applicable

This context will be provided verbatim to each reviewer subagent.

### Step 2b — Chunk Large Targets

If the code under review exceeds ~300 lines, split it into logical segments
(by file, pass, or function group) and run Steps 3–5 independently on each
segment. After all segments are reviewed, merge and deduplicate findings across
segments before the final report.

This ensures every section receives full attention. Do not skip this step for
large targets — reviewer quality degrades significantly when a single prompt
must cover hundreds of lines.

### Step 3 — Launch Four Constructive Reviewer Subagents in Parallel

Spawn four fresh subagents using the named agents: `conservative-lens`,
`security-lens`, `usability-lens`, and `speed-lens`. Each agent's `.agent.md`
already defines its identity, mission, focus areas, and guardrails — **do not
restate these**. Each receives the same prompt (below) with the code, context,
and shared review protocol.

**Do NOT include the adversarial reviewer in this step.** It runs later in
Step 4b.

**Each subagent is independent — do not allow one reviewer's output to
influence another.**

#### Shared Review Prompt

Send this prompt to each of the four subagents, substituting `{code}` and
`{context}`:

```
TASK: Code review. Apply your perspective (per your agent definition) to the
code below.

Your goal is to find ALL issues, not just prominent ones. A review that finds
8 real issues is better than one that finds 3 obvious ones.

=== PHASE 1 — INVENTORY ===
Before writing findings, list every function, rewrite rule, pass definition,
match arm, and significant code block in the code. Number them. This ensures
systematic coverage.

=== PHASE 2 — ASSESS ===
For each inventory item, check it against your focus areas. If no issues,
write "No issues." Do not skip items.

Additionally, for rewrite rules verify:
- Pattern matches are specific enough (not overly broad)
- WF spec consistency with the rule's output
- Fixed-point convergence (rules that always match must make progress)
- Error handling produces proper Error nodes
- Symbol table interactions are safe

=== FINDING FORMAT ===
For each issue:
- ID: a short identifier (e.g., SPD-1, SEC-3, USA-2, CON-1)
- Describe the issue concretely
- Quote the specific code
- Severity: CRITICAL / HIGH / MEDIUM / LOW
- Suggest a specific fix
- For bugs: provide a minimal test case, input, or execution trace that
  demonstrates the issue. If you cannot construct one, mark as UNVERIFIED.

Severity scale:
- CRITICAL: Silent wrong output, unbounded resource consumption, or
  exploitable vulnerability
- HIGH: Crash, assert failure, or data corruption on reachable input
- MEDIUM: Incorrect error message, suboptimal performance, edge case drift,
  or defence-in-depth concern
- LOW: Style, hardening opportunity, or micro-optimization

CODE UNDER REVIEW:
{code}

CONTEXT:
{context}
```

### Step 4 — Synthesize Constructive Findings

After all four constructive reviewers return, synthesize their outputs:

1. **Deduplicate** — merge issues raised by multiple reviewers into a single
   entry, noting which perspectives flagged it.
2. **Classify** each unique issue by type: Security, Correctness, Performance,
   Quality.
3. **Assign final severity** using this priority order:
   - **CRITICAL**: Exploitable vulnerabilities, or silent wrong output
   - **HIGH**: Likely crashes, data corruption, or significant code quality
     problems
   - **MEDIUM**: Defence-in-depth concerns, edge-case issues, moderate quality
     issues, measurable performance problems
   - **LOW**: Hardening opportunities, style issues, micro-optimizations

### Step 4b — Adversarial Gap-Analysis Pass

After synthesizing the four constructive reviewers' findings, spawn a fresh
`adversarial-lens` subagent. Its agent definition already provides identity,
mission, and adversarial focus including gap-analysis mode instructions —
**do not restate these**. Provide it:
- The full code under review (verbatim)
- The context
- The **complete synthesized findings from Step 4** so it knows what was
  already found

Use this prompt:

```
TASK: Adversarial gap-analysis code review. Four other reviewers (conservative,
security, usability, performance) have already reviewed this code. Their
findings are listed below under EXISTING FINDINGS. Your job is to find what
they MISSED. Follow the Gap-Analysis Mode instructions in your agent definition.

Do NOT re-report existing findings. Only report NEW issues.

CODE UNDER REVIEW:
{code}

CONTEXT:
{context}

EXISTING FINDINGS:
{synthesized_findings_from_step_4}
```

Merge the adversarial reviewer's new findings into the synthesized list, then
proceed to verification.

### Step 5 — Verify Issues

For issues rated CRITICAL or HIGH:

1. **Attempt reproduction** — write or describe a minimal test case, code path,
   or input that demonstrates the issue. Use the Explore subagent or terminal
   to check whether the issue is real.
2. **Cross-reference** the relevant WF specs and pass definitions to confirm
   whether the behavior is actually wrong.
3. **Mark each issue** as:
   - **Verified** — reproduced or confirmed by code inspection
   - **Likely** — strong evidence but not yet reproduced
   - **Unverified** — plausible but needs investigation

Downgrade or remove issues that cannot be substantiated after reasonable
investigation.

### Step 6 — Present the Review Report

Present findings in this structure:

```markdown
## Code Review: {target description}

### Summary
{One paragraph overview: what was reviewed, key findings, overall assessment}

### Critical & High Issues
{Table or numbered list, each with: description, location, severity,
verification status, which reviewer(s) flagged it}

### Medium Issues
{Same format}

### Low Issues
{Same format, can be briefer}

### Remediation Plan
{Ordered list of recommended fixes, grouped by priority, with specific code
changes or next steps for each}

### Positive Observations
{Brief note of things done well — reviewers should acknowledge good patterns}
```

## Severity Tiebreaker

When severity is ambiguous, resolve using the project's decision priorities:

1. Correctness
2. Security
3. Usability / Maintainability
4. Performance

## Guardrails

- Each reviewer subagent must be a fresh instance — no context contamination
  between reviewers.
- Do not skip the verification step for CRITICAL/HIGH issues. Unverified
  critical findings must be explicitly marked as such.
- Do not propose fixes that would change pass pipeline semantics without
  explicit callout.
- Prefer concrete code suggestions over vague recommendations.
- The adversarial reviewer runs AFTER the constructive reviewers, not in
  parallel. It receives the synthesized findings to avoid duplicate work and
  focus on gaps.
