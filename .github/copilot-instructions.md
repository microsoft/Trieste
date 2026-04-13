# Trieste — Copilot Instructions

## What is Trieste?

Trieste is a **header-only C++20 term rewriting system** for rapidly prototyping programming languages. It provides three C++ embedded DSLs:

1. **Parsing DSL** — regex-based tokenization that produces an untyped AST from source files.
2. **Rewriting DSL** — pattern matching and term rewriting to reshape ASTs through successive passes.
3. **Well-formedness (WF) DSL** — declarative specifications of valid AST shapes, used for validation and automatic fuzz-test generation.

Language implementation in Trieste is a pipeline: `Parse → Pass₁ → Pass₂ → ... → Passₙ → Output`, where each pass rewrites the AST from one well-defined shape to another.

## Core Concepts

### Tokens (`include/trieste/token.h`)

Tokens are the type system for AST nodes. Each is defined as an `inline const auto` at namespace scope:

```cpp
inline const auto Ident = TokenDef("infix-ident", flag::print);
inline const auto Calculation = TokenDef("infix-calculation", flag::symtab | flag::defbeforeuse);
```

Token names are prefixed with the language name (e.g. `infix-`, `json-`) to avoid collisions. Flags control node behavior:
- `flag::print` — display source text when printing the AST
- `flag::symtab` — node carries a symbol table
- `flag::defbeforeuse` — symbols must be defined before use
- `flag::lookup` / `flag::lookdown` — symbol visibility directions
- `flag::shadowing` — stops lookup ascending to parent scopes

Built-in tokens include `Top`, `File`, `Group`, `Error`, `ErrorMsg`, `ErrorAst`, `Lift`, `Seq`, `NoChange`.

### AST Nodes (`include/trieste/ast.h`)

`Node` is an intrusive reference-counted pointer to `NodeDef`. Each node has a Token type, a Location (source position), a parent pointer, children (`std::vector<Node>`), and an optional symbol table. The `<<` operator builds trees fluently: `Assign << ident << expr`.

### Parsing (`include/trieste/parse.h`)

The `Parse` class implements a mode-based regex tokenizer. Rules are defined per mode as `regex >> lambda`:

```cpp
Parse p(depth::file, wf_parser);
p("start", {
    "[[:blank:]]+" >> [](auto&) {},
    R"([[:digit:]]+\b)" >> [](auto& m) { m.add(Int); },
});
```

The `Make` object provides `m.add()`, `m.push()`, `m.pop()`, `m.seq()`, `m.term()`, `m.mode()`, and `m.error()` for building the AST during parsing.

### Rewriting (`include/trieste/rewrite.h`)

Rewrite rules use operator overloading for pattern matching: `pattern >> effect`:

```cpp
In(Expression) * (T(Ident)[Id] * T(Add) * T(Ident)[Rhs]) >>
  [](Match& _) { return Add << (Expression << _(Id)) << (Expression << _(Rhs)); };
```

Pattern combinators:
- `T(Token)` — match a node type; `T(A, B, C)` — match any of those types
- `In(Token)` — parent must be that type; `In(Token)++` — any ancestor must be that type
- `P * Q` — sequence; `P / Q` — choice
- `P[Name]` — capture node as `Name`; access via `_(Name)` or `_[Name]` (range)
- `~P` — optional; `P++` — zero or more; `!P` — negation
- `Start`, `End`, `Any` — positional/wildcard matchers

Effects return `Node` (replacement), `NoChange` (skip), or `Seq` (splice multiple nodes).

### Passes (`include/trieste/pass.h`)

A `PassDef` bundles rewrite rules with a name, WF spec, and direction:

```cpp
PassDef my_pass = {
    "pass_name",
    wf_after_this_pass,
    dir::topdown,  // or dir::bottomup, dir::once
    { /* rewrite rules */ }
};
```

Passes iterate to a fixed point unless `dir::once` is set. Additional hooks: `pre()`, `post()`, `cond()`.

### Well-formedness (`include/trieste/wf.h`)

WF specs declare valid AST shapes:

```cpp
// clang-format off
const auto wf =
    (Top <<= Calculation)
  | (Calculation <<= (Assign | Output)++)
  | (Assign <<= Ident * Expression)[Ident]
  | (Expression <<= (Add | Subtract | Ref | Int))
  ;
// clang-format on
```

Key operators: `<<=` (define shape), `|` (choice/combine), `++` (zero or more), `~` (optional), `*` (fixed ordered fields), `[Token]` (symbol binding). WF specs are defined incrementally — each pass extends/overrides the previous spec using `|`.

### Reader (`include/trieste/reader.h`)

Combines a parser + ordered list of passes into a pipeline:

```cpp
Reader reader() {
    return {"json", {groups(), structure()}, parser()};
}
```

### Writer (`include/trieste/writer.h`)

Converts an AST back to output files via passes that produce `Directory`/`File`/`Contents` nodes.

### Rewriter (`include/trieste/rewriter.h`)

A lightweight wrapper for running additional pass pipelines on an already-parsed AST. Chains with `>>`: `reader >> rewriter`.

### Driver (`include/trieste/driver.h`)

Wraps a Reader with CLI11-based CLI providing `build` (parse + rewrite + output) and `test` (WF-driven fuzz testing) subcommands:

```cpp
int main(int argc, char** argv) {
    return trieste::Driver(my_reader()).run(argc, argv);
}
```

## Project Organisation

### `include/trieste/` — Header-only library

| Header | Purpose |
|--------|---------|
| `trieste.h` | Master include + pipeline operators (`>>`) |
| `token.h` | Token/TokenDef types, flags, built-in tokens |
| `ast.h` | NodeDef, symbol tables, tree manipulation |
| `source.h` | Source files, Location |
| `parse.h` | Parse DSL (regex rules, Make context) |
| `rewrite.h` | Pattern matching DSL |
| `pass.h` | PassDef (rules + WF + direction) |
| `wf.h` | Well-formedness DSL + random AST generation |
| `passes.h` | PassRange, Process, ProcessResult |
| `reader.h` | Reader (parse + passes pipeline) |
| `writer.h` | Writer + Destination |
| `rewriter.h` | Rewriter (pass-only pipeline) |
| `driver.h` | CLI Driver (build/test subcommands) |
| `regex.h` | RE2-compatible TRegex API (backed by `regex_engine.h`) |
| `regex_engine.h` | NFA-based regex engine (compile + simulate) |
| `unicode_data.h` | Unicode 15.1 General Category tables + ASCII bitmaps |
| `gen.h` | Random generation utilities |
| `fuzzer.h` | Fuzzer support |
| `intrusive_ptr.h` | Reference-counted smart pointer |
| `logging.h` | Logging system |
| `utf8.h` | UTF-8 utilities |
| `defaultmap.h` | Hash map for rule dispatch |
| `checker.h` | AST well-formedness checker |
| `debug.h` | Debug utilities |
| `nodeworker.h` | Parallel node worker |
| `xoroshiro.h` | RNG for fuzzing |

### `parsers/` — Real-world parsers

- **`json/`** — Complete JSON parser and writer with JSON Patch support. Files: `parse.cc` (tokenizer), `reader.cc` (rewrite passes), `json.cc` (JSON Pointer/Patch operations), `writer.cc` (AST → JSON string).
- **`yaml/`** — YAML parser with JSON conversion.
- **`test/`** — Test suites for both parsers.
- Shared parser headers are in `parsers/include/trieste/`.

### `samples/` — Example languages

- **`infix/`** — Tutorial calculator language demonstrating the full pipeline with 5 reader passes, writer passes for infix and postfix output, and a `calculate` rewriter for evaluation.
- **`shrubbery/`** — Another sample language.

### `test/` — Core library tests

- `intrusive_ptr_test.cc` — reference counting tests
- `source_test.cc` — source file handling tests
- `regex_engine_test.cc` — regex engine unit tests
- `regex_engine_benchmark.cc` — regex engine benchmark (vs RE2 when enabled)
- `tregex_benchmark.cc` — TRegex API benchmark
- `nodeworker_test.cc` — parallel node worker tests

### `notes/` — Design documents

Design notes covering lookup semantics, control flow, dispatch, packages, regions, etc.

### Build system

CMake with `FetchContent` for dependencies:
- **snmalloc** — memory allocator (optional override of new/delete)
- **CLI11** — command-line parsing
- **RE2** — fetched only when `TRIESTE_BUILD_REGEX_BENCHMARK=ON` for benchmark comparisons

Key CMake options:
- `TRIESTE_BUILD_SAMPLES` (ON) — build sample languages
- `TRIESTE_BUILD_PARSERS` (ON) — build JSON/YAML parsers
- `TRIESTE_ENABLE_TESTING` (OFF) — enable core tests
- `TRIESTE_BUILD_PARSER_TESTS` (OFF) — enable parser tests
- `TRIESTE_USE_CXX17` (OFF) — target C++17 instead of C++20
- `TRIESTE_USE_SNMALLOC` (ON) — override new/delete with snmalloc

## Conventions

- **C++20** (or C++17 compat), header-only core.
- **Naming**: `snake_case` for functions/variables, `PascalCase` for Token names.
- **Namespaces**: `trieste::` for core; language-specific namespaces (`infix::`, `json::`, `yaml::`) for implementations.
- **WF formatting**: use `// clang-format off` / `// clang-format on` around WF definitions to preserve alignment.
- **Error handling**: errors are represented as AST nodes using `Error << (ErrorMsg ^ "msg") << (ErrorAst << node)`, exempt from WF checking.
- **Testing**: the Driver's `test` subcommand generates random ASTs from WF specs and fuzz-tests each pass automatically.
- **Pass functions**: each pass is typically a function returning `PassDef`, defined in an anonymous namespace for internal passes.

## Workflow

**Go slow to go fast.** Favour small, incremental changes over large sweeping ones. Each change should be easy to understand, review, and — if needed — revert. Resist the temptation to bundle unrelated improvements together. A sequence of small, correct steps reaches the goal faster than a single big leap that introduces subtle bugs.

### Local Automation Artifacts

When creating temporary or task-local automation (scripts, one-off programs,
generated reports, or helper files), store them under `.copilot/` in the repo
instead of project source directories. Treat `.copilot/` as the default location
for Copilot-authored workflow tooling unless the user requests a different
destination.

When asked to complete a task, follow this process:

1. **Understand** — Read the relevant code and gather enough context to fully understand what is being asked.
2. **Baseline** — Before making any changes, run the relevant test suite and record the results. See the baseline section below.
3. **Plan** — Follow the **Multi-perspective Planning Process** described below to produce a plan.
4. **Confirm** — Present the synthesised plan to the user and **wait for explicit approval** before making any edits. If the user requests changes to the plan, revise and re-confirm.
5. **Record** — Once the plan is approved, save it to a session memory file (e.g. `/memories/session/plan.md`). Include the goal, the numbered steps, files to modify, and the test baseline. Keep this file in context throughout implementation and update it as steps are completed.
6. **Implement** — Once approved, carry out the plan **one step at a time, strictly in order**. Do not skip ahead, parallelise steps, or combine multiple plan steps into a single change. Complete each step (edit → compile → test → review) before starting the next. Move slow to go fast.
7. **Verify** — After implementing **each step**, check for compile errors and run any applicable tests. Report the outcome and compare against the baseline. Do not proceed to the next step until the current step compiles cleanly and all previously-passing tests still pass.
8. **Review** — Follow the review loop described below.

### Multi-perspective Planning Process

When planning a code change to the library, use four constructive lens agents to generate competing plans, synthesise them with explicit rebuttal resolution, then stress-test the result with an adversarial reviewer.

#### Step 1 — Gather context

Read the relevant code and gather enough context to fully describe the task to the lens agents. The lens agents are fresh subagents with no prior context — provide them with everything they need: the task description, relevant file paths and code excerpts, WF specs, token definitions, and any constraints or requirements.

#### Step 2 — Gather sub-plans

Spawn **four subagents in parallel**, one for each constructive lens agent. Each receives the same task description and context but plans through a different lens:

| Agent | Focus |
|-------|-------|
| `speed-lens` | Runtime performance, low allocations, minimal passes, cache efficiency |
| `security-lens` | Defence in depth, safe error handling, bounded resources, fuzz coverage |
| `usability-lens` | Clarity, readability, correctness, consistent naming, one-concept-per-pass |
| `conservative-lens` | Smallest diff, maximum reuse, no speculative generality, backwards compat |

Invoke each lens agent with:
> You are planning a change to the Trieste library. Here is the task: [task description and relevant context]. Produce a numbered plan following the output format defined in your instructions.

#### Step 3 — Identify conflicts and run rebuttals

Read all four lens outputs and identify *substantive design conflicts* — cases where two or more lenses propose incompatible approaches ("use A" vs. "use B" where both cannot coexist). Different emphasis on the same approach is not a conflict.

If conflicts are found:
- For each conflict, spawn the disagreeing lens agents in parallel (fresh subagents, by name). Each receives: (a) the specific conflict description, (b) its own original recommendation, (c) the opposing recommendation(s), and (d) instructions to make its strongest case for why its approach should be chosen, directly addressing the opponent's arguments.
- **One rebuttal round only** — no counter-rebuttals. The adversarial review loop (step 5) catches remaining issues.
- If no substantive conflicts are found, skip this step entirely.

#### Step 4 — Evaluate and synthesise

Review the four sub-plans yourself and produce a short evaluation covering:

- **Convergence**: where two or more plans agree on the same approach.
- **Unique insights**: ideas that appear in only one plan and are worth incorporating.
- **Conflicts**: where plans disagree. For each conflict, summarise the rebuttal arguments from each side (if rebuttals were run) and state which perspective you favour and why.
- **Gaps**: anything none of the four plans addressed.

Then spawn a **fresh `synthesis-lens` subagent**. Provide it with:
- The original task description.
- All four sub-plans (labelled by perspective).
- Any rebuttal arguments (labelled by conflict and perspective).
- Your evaluation.

When rebuttals are present, synthesis receives structured arguments for each side rather than inferring them. The synthesis lens must engage with the specific rebuttal arguments made rather than ignoring them.

#### Step 5 — Adversarial review loop

Run the synthesised plan through an iterative adversarial review:

1. Spawn a **fresh `adversarial-lens` subagent**. Provide it with the synthesised plan and full task context. Ask it to attack the plan: find inputs, interactions, and assumptions that would break it.
2. If the adversarial review finds issues:
   a. Fix the plan yourself, addressing each accepted finding with a specific defence (test case, bounds check, or design constraint).
   b. If any finding is unclear or you are unsure how to address it, ask the user for guidance before proceeding.
   c. Spawn a **new fresh `adversarial-lens` subagent** to review the revised plan. (Each review must use a fresh subagent.)
3. If the review comes back clean (no actionable findings), proceed to Step 6.
4. If the loop has run **5 times** without converging, proceed to Step 6 anyway — present the remaining unresolved findings to the user for decision.

#### Step 6 — Present for approval

Present the reviewed plan to the user along with a brief summary of:
- Key points of agreement across the four lens agents.
- Notable trade-offs made during synthesis.
- Conflicts resolved via rebuttals and which perspective prevailed.
- Any minority opinions from individual lens agents that were overruled.
- Issues caught and resolved during the adversarial review loop.
- Any unresolved adversarial findings (if the loop hit the 5-iteration cap).

### Execution Rules

- Fresh subagents for each phase (lens, rebuttal, synthesis, adversarial review) — no context contamination.
- Four constructive lens subagents run in parallel; conflict identification, rebuttals, synthesis, adversarial review, and user-facing presentation run sequentially.
- Rebuttal subagents for a single conflict run in parallel with each other (they argue independently).
- Lens phases are independent — do not allow one lens's output to shape another's. Rebuttals are a second pass; the original independent outputs are preserved and forwarded to synthesis alongside rebuttals.
- Synthesis must resolve disagreements explicitly, not average them away. When rebuttals are available, synthesis must engage with the arguments made rather than ignoring them.
- The adversarial review loop is mandatory before presenting to the user. Each iteration uses a fresh adversarial subagent.

### Review Loop

Every non-trivial change must go through an iterative review cycle:

1. After implementing, spawn a subagent to review the change. Provide it with full context: the goal, the files modified, and what to look for (correctness, style, consistency with project conventions).
2. Address the review findings, then spawn a **different** subagent to review the updated change.
3. Repeat until a review comes back clean (no issues found).

**Skipping the review loop** — You do not decide whether a change is trivial. If you believe the review loop can be skipped, ask the user for explicit permission first.

**The user's role** — This review loop replaces the user as gatekeeper but **not** as collaborator. If review comments are unclear or you are unsure how to address them, ask the user for guidance before proceeding.

### Test Baseline

Before making any changes, run the relevant tests and **keep the results in context** (e.g. in a session memory note or a todo list entry). This establishes which tests already pass and which, if any, already fail.

- **Why**: Without a baseline, you cannot tell whether a failure was introduced by your change or was pre-existing. This leads to wasted effort chasing phantom regressions — or, worse, silently accepting real ones.
- **What to record**: The exact command run, the total pass/fail counts, and the names of any pre-existing failures.
- **When to compare**: After every implementation step, re-run the tests and compare against the baseline. Any new failure is your responsibility to investigate before moving on.

### Root-cause Thinking

When something fails — a build error, a test failure, unexpected behaviour — resist the urge to patch the immediate symptom. Instead:

1. **Diagnose** — Trace the problem back to its origin. Ask *why* the failure occurred, not just *what* failed.
2. **Understand** — Read the surrounding code and design to understand the intent. A symptom in one file often has its root cause in another.
3. **Fix the cause** — Address the underlying issue. If a fix feels like a workaround or special case, that is a signal to dig deeper.
4. **Validate** — Confirm that the root-cause fix resolves the symptom *and* does not introduce new issues elsewhere.

### Incremental Implementation

When creating a new subsystem (more than roughly 200 lines), do not write the entire implementation in one go and test at the end. Instead:

1. **Build in testable increments.** After each increment, write a minimal standalone test or use an existing test suite to verify correctness before proceeding.
2. **Run integration tests early.** After replacing a dependency, run the project's own test suite before writing new unit tests. The project's tests exercise real-world usage patterns that isolated unit tests miss.
3. **Inspect intermediate representations.** When an NFA, FSM, compiler, or pass pipeline produces wrong results, dump its intermediate representation (state tables, AST, bytecode) before reasoning about execution logic. The structure reveals the bug faster than tracing execution paths.

### Step Ordering — Move Slow to Go Fast

When a plan has numbered steps, **execute them strictly in sequence, one at a time**. This is a hard rule, not a guideline.

- **One step at a time.** Do not begin step N+1 until step N is fully complete: code written, compiled, tests passing, and reviewed.
- **No skipping.** Even if a later step seems independent, execute it in its planned order. Hidden dependencies are the norm; apparent independence is often an illusion.
- **No combining.** Do not merge multiple plan steps into a single commit or editing session. Each step is its own unit of work with its own compile–test–review cycle.
- **Verify after every step.** Re-run the relevant test suite and compare against the baseline. Any new failure must be investigated and resolved before moving on.
- **Record progress.** After completing each step, update the session plan file to mark it done. This provides a clear audit trail and makes it safe to resume after interruptions.

### Review Bug Reports Must Be Reproducible

When a review subagent reports a bug, the finding must include a minimal test case or execution trace that demonstrates the failure. If the reviewer cannot construct one, the finding should be marked **unverified**. Prefer spawning a subagent that compiles and runs a minimal reproduction over one that only reads code and reasons theoretically.

### Planning Calibration

The four-perspective planning process is most valuable for **design decisions** — API shape, architecture, pass structure, and trade-offs that affect downstream consumers. For tasks that are primarily **implementation of a well-understood algorithm** (e.g. Thompson NFA, topological sort, standard data structure), a single conservative plan with emphasis on incremental testing is sufficient. Use the full four-perspective process when the design is uncertain, not when the algorithm is known and the work is primarily about correct implementation.
