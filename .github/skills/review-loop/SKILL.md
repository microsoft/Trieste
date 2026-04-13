---
name: review-loop
description: "Iterative review loop using any lens agent against any target (plan, code, design). Use when you want to run a focused review cycle: spawn a fresh reviewer subagent with a specific lens, present findings and remediation to the user, apply fixes with user guidance, and repeat until clean."
---

# Review Loop

Use this skill to run an iterative review cycle against any target using any
lens agent.

## Inputs

The user specifies:

1. **Lens** — which lens/reviewer to use (e.g., `adversarial-lens`,
   `security-lens`, `conservative-lens`, `speed-lens`, `usability-lens`).
2. **Target** — what to review (a plan, file(s), module, design document,
   diff, etc.).

If either is ambiguous, ask the user before proceeding.

## Procedure

### Step 1 — Gather Context

Read the target material and any surrounding context needed for a meaningful
review. This includes:
- The target itself (plan text, file contents, etc.)
- Relevant WF specs, token definitions, and pass pipeline context
- Relevant tests or prior review findings

### Step 2 — Spawn Reviewer Subagent

Spawn a **fresh** subagent with the specified lens. Provide it:
- The full target material (verbatim — do not summarize)
- Sufficient surrounding context for the reviewer to assess the target
  meaningfully
- Instructions to produce structured findings per the lens's Expected Output
  format
- Instructions to explicitly state if the target is clean (no findings)

The subagent must be a fresh instance with no prior context from this
conversation.

### Step 3 — Present Results

Present the reviewer's findings to the user along with a proposed remediation
plan:
- List each finding with its severity/likelihood
- For each finding, propose a specific remediation (code change, plan
  amendment, verification step, etc.)
- If the reviewer found no issues, report that the target passed review cleanly

### Step 4 — Apply Remediations

With the user's guidance:
- Apply agreed-upon remediations (edit files, amend plans, add tests, etc.)
- Skip or defer any remediations the user rejects
- If the user modifies a remediation, apply their version

### Step 5 — Loop or Exit

Ask the user whether to:
- **Loop** — go back to Step 2 and run the review again on the updated target
  with a fresh subagent
- **Exit** — the review cycle is complete

## Guardrails

- Every review iteration uses a **fresh subagent** — no context contamination
  between iterations.
- Do not summarize or filter the target material before passing it to the
  reviewer. Provide it verbatim.
- Do not apply remediations without user confirmation.
- The loop exits only when the user says so, not when the reviewer returns
  clean. The user may want to run additional iterations even after a clean pass.
