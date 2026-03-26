---
name: review-multi-perspective
description: >
  Multi-perspective code review skill for Trieste library changes. Spawns four
  parallel review subagents — Speed, Security, Usability, and Conservative —
  each examining the same target through a different lens. Findings are
  synthesized into a unified, prioritized remediation plan for user approval.
  Use this skill when reviewing code changes, pull requests, or existing
  subsystems and a thorough, balanced review is needed.
---

# Multi-Perspective Code Review

You are conducting a structured code review using four specialized perspectives.
The goal is to surface issues that a single-lens review would miss, then
synthesize the findings into a prioritized remediation plan.

## When to Use This Skill

- Reviewing a completed implementation before merging.
- Auditing an existing subsystem for improvement opportunities.
- Post-implementation review of a multi-step plan.
- When the user asks for a "thorough review", "multi-perspective review", or
  "code review" of a target.

## Process Overview

```
Target ──► 4 parallel review subagents ──► Evaluate findings ──► Synthesize
              (Speed, Security,              (convergence,         remediation
               Usability, Conservative)       conflicts, gaps)     plan
                                                                    │
                                                              Present to user
                                                              for approval
```

## Step 1 — Identify the Review Target

Before spawning reviewers, clearly define the **review scope**:

- **Files**: the specific files or file ranges to review.
- **Context**: what the code does, what change was made, and the intended
  behavior. Include relevant WF specs, pass definitions, and token declarations.
- **Baseline**: any test results, known issues, or pre-existing failures that
  reviewers should be aware of.

Gather this context yourself by reading the relevant files. Do not ask the user
to provide what you can discover.

## Step 2 — Spawn Four Review Subagents

Spawn **four subagents in parallel**, each prompted with the review target and
one of the following review lenses. Each subagent must read the target files
and produce findings independently.

### Review Lenses

| Subagent | Lens | Focus |
|----------|------|-------|
| Speed Reviewer | `/review-speed` | Unnecessary allocations, redundant traversals, suboptimal pattern dispatch, pass count bloat, missing `dir::once`, cache-unfriendly access patterns, algorithmic complexity issues |
| Security Reviewer | `/review-security` | Unbounded recursion/iteration, missing validation at boundaries, unsafe node access across rewrite boundaries, missing Error nodes for failure paths, overly broad symbol visibility, inadequate fuzz coverage, regex safety |
| Usability Reviewer | `/review-usability` | Unclear naming, passes doing multiple things, imprecise WF specs, inconsistent patterns, poor error messages, hard-to-follow control flow, missing or misleading documentation |
| Conservative Reviewer | `/review-conservative` | Unnecessary new abstractions, changes that could have reused existing infrastructure, public API surface growth, backwards compatibility risks, ripple effects on downstream WF specs, over-engineering |

### Subagent Prompt Template

Each subagent receives the following prompt (with `{lens}` and `{focus}`
substituted):

```
You are reviewing code in the Trieste library through the lens of {lens}.

## Review Target
{description of the target, files, and context}

## Your Focus
{focus description from the table above}

## Instructions
1. Read all target files thoroughly.
2. For each finding, provide:
   - **ID**: a short identifier (e.g., `SPD-1`, `SEC-3`, `USA-2`, `CON-1`).
   - **Severity**: critical, major, minor, or suggestion.
   - **Location**: file path and line number(s).
   - **Finding**: what the issue is.
   - **Evidence**: the specific code or pattern that demonstrates the issue.
     Quote the relevant lines.
   - **Recommendation**: what should change and why.
   - **Reproduction** (for bugs): a minimal test case, execution trace, or
     concrete input that triggers the issue. If you cannot construct one, mark
     the finding as **unverified**.
3. Do NOT report style or formatting issues unless they obscure correctness.
4. Do NOT report issues outside your assigned lens — other reviewers cover those.
5. Order findings by severity (critical first).
6. At the end, provide a **summary** with: total findings by severity, the
   single most important issue, and an overall assessment from your perspective.
```

## Step 3 — Evaluate Findings

After all four subagents return, evaluate their combined findings:

### 3a. Deduplicate

Identify findings from different reviewers that describe the same underlying
issue. Merge them into a single finding, noting which perspectives flagged it
(this increases confidence).

### 3b. Classify Convergence

- **High convergence** (3–4 reviewers flagged): almost certainly a real issue.
  Prioritize highly.
- **Medium convergence** (2 reviewers flagged): likely real. Include in the
  remediation plan.
- **Single-reviewer findings**: may be lens-specific. Include if severity is
  major or critical; otherwise include as suggestions.

### 3c. Identify Conflicts

Where reviewers disagree (e.g., Speed says "merge these passes" but Usability
says "keep them separate"), note the conflict and apply this resolution order:

1. **Correctness** wins over all other concerns.
2. **Security** wins over speed and usability.
3. **Usability** wins over speed (clear code is easier to optimize later).
4. **Speed** wins when the other concerns are already satisfied.
5. **Conservative** acts as a tiebreaker — when other concerns are equal, prefer
   the smaller change.

### 3d. Flag Unverified Findings

Any finding without a concrete reproduction (test case, execution trace, or
specific input) is marked **unverified**. Unverified findings appear in the
plan but at reduced priority unless their severity is critical.

### 3e. Identify Gaps

Note anything that none of the four reviewers addressed but that seems relevant
given the review target (e.g., missing test coverage, undocumented assumptions).

## Step 4 — Synthesize the Remediation Plan

Produce a single remediation plan organized as follows:

### Plan Format

```markdown
# Remediation Plan

## Summary
- Total findings: N (X critical, Y major, Z minor, W suggestions)
- Convergence: N findings flagged by multiple reviewers
- Unverified: N findings without reproduction

## Critical Findings
For each critical finding:
### [ID] Title
- **Severity**: critical
- **Flagged by**: Speed, Security (list which reviewers found it)
- **Location**: file(s) and line(s)
- **Issue**: description
- **Evidence**: quoted code
- **Remediation**: specific steps to fix
- **Verification**: how to confirm the fix (test, compile check, etc.)

## Major Findings
(same format as critical)

## Minor Findings
(same format, briefer)

## Suggestions
(same format, briefest)

## Unverified Findings
(same format, with note on why reproduction was not possible)

## Conflicts Resolved
For each conflict between reviewers:
- **Conflict**: what the disagreement was
- **Resolution**: which perspective was favoured and why

## Implementation Order
A numbered list of remediation steps in recommended execution order,
considering dependencies between fixes. Each step references the finding ID(s)
it addresses.
```

## Step 5 — Present for Approval

Present the remediation plan to the user. Include a brief executive summary:

- How many findings total, broken down by severity.
- Key points of agreement across reviewers.
- Notable trade-offs or conflicts that were resolved.
- Any unverified findings that warrant manual investigation.

**Wait for explicit user approval** before implementing any remediation steps.
The user may:
- Approve the full plan.
- Approve specific findings and reject others.
- Request additional investigation of unverified findings.
- Ask for re-review of specific aspects.

## Step 6 — Implement (After Approval)

Once approved, implement the remediation plan following the standard workflow:

1. Record the approved plan in session memory.
2. Execute steps strictly in order, one at a time.
3. After each step: compile, run tests, compare against baseline.
4. After all steps: run a final review subagent (not multi-perspective — a
   single focused review) to confirm the remediations are correct and complete.

## Severity Definitions

| Severity | Definition | Examples |
|----------|-----------|----------|
| **Critical** | Causes incorrect behavior, crashes, security vulnerabilities, or data loss. Must fix before merging. | Unbounded recursion on crafted input; node access after rewrite invalidation; missing Error node causing WF violation |
| **Major** | Significant quality issue that should be fixed but doesn't cause immediate failure. | O(n²) algorithm where O(n) exists; pass doing two unrelated things; WF spec allowing shapes that downstream passes can't handle |
| **Minor** | Small quality issue. Fix if convenient, acceptable to defer. | Suboptimal pattern ordering; slightly unclear naming; redundant node creation |
| **Suggestion** | Improvement idea, not a defect. Informational. | "Consider using `dir::once` here"; "This token name could be more descriptive" |

## Trieste-specific Review Guidance

- **WF spec tightness**: Every pass should have a WF spec that is as tight as
  possible. Overly permissive specs hide bugs.
- **Error propagation**: Error nodes must never be silently dropped. Check that
  every rewrite rule that matches `Any` or broad patterns preserves Error nodes.
- **Symbol table scope**: `flag::symtab`, `flag::lookup`, `flag::lookdown`, and
  `flag::shadowing` are security-relevant — review their use carefully.
- **Pass direction**: Verify that the chosen direction (`topdown`, `bottomup`,
  `once`) is correct for the transformation. A wrong direction can cause missed
  rewrites or infinite loops.
- **Fixed-point termination**: Passes without `dir::once` iterate to a fixed
  point. Verify that every rule makes progress (reduces the tree or changes a
  token type) to guarantee termination.
- **Pattern specificity**: Overly broad patterns (leading `Any++`) cause
  unnecessary rule firings. Check that `In()` contexts and leading `T()` tokens
  are as specific as possible.
