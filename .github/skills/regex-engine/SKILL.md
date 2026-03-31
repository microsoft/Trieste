---
name: regex-engine
description: >
  Design guide for the Trieste regex engine (include/trieste/regex_engine.h).
  Describes the architecture, data structures, and conventions of the
  header-only NFA-based regex engine used for pattern matching in the Trieste
  library. Use this skill when modifying, extending, or debugging the regex
  engine.
user-invocable: false
---

# Regex Engine Design

The Trieste regex engine is a header-only C++20 NFA-based matcher in
`include/trieste/regex_engine.h`. It compiles UTF-8 regexes with a
shunting-yard + Thompson pipeline and simulates them with epoch-based state
deduplication.

The engine started as an iregexp-focused implementation, but now includes
compatibility extensions used by Trieste parsers and rewrite helpers.

## Architecture Overview

The engine processes a regex in four phases:

```
regex string ──► postfix runes ──► NFA ──► finalization ──► simulation
               (shunting-yard)   (Thompson)  (closures+bitmaps) (parallel)
```

1. **Shunting-yard** (`regexp_to_postfix_runes`): Converts regex text to a
   postfix rune stream with explicit operators.
2. **Thompson's construction** (`postfix_to_nfa`): Walks the postfix sequence
   and builds an NFA from `StateDef` nodes using a fragment stack.
3. **Finalization** (`precompute_epsilon_closures` + `finalize_states`):
   Precomputes epsilon closures (with trivial-closure detection) and
   populates per-state ASCII acceptance bitmaps.
4. **Parallel simulation** (`match` / `find_prefix`): Tracks active NFA
   states using precomputed epsilon closures and per-call `MatchContext` state.

## Headers

| Header | Purpose |
|--------|---------|
| `regex_engine.h` | Engine implementation (all phases, `RuneClass`, sentinels) |
| `unicode_data.h` | Unicode 15.1 General Category range tables + precomputed ASCII bitmaps |
| `utf8.h` | UTF-8 encoding/decoding, `rune_t` type definition |

`regex_engine.h` includes both `unicode_data.h` and `utf8.h`.

## Public API

```cpp
RegexEngine(const std::string_view& utf8_regexp);
RegexEngine(const std::string_view& utf8_regexp, SyntaxMode mode);
bool ok() const;
size_t num_captures() const;
SyntaxMode syntax_mode() const;
bool match(const std::string_view& utf8_str) const;
bool match(
  const std::string_view& utf8_str,
  MatchContext& ctx) const;
size_t find_prefix(
  const std::string_view& utf8_str,
  bool at_start = true) const;
size_t find_prefix(
  const std::string_view& utf8_str,
  MatchContext& ctx,
  bool at_start = true) const;
size_t find_prefix(
  const std::string_view& utf8_str,
  std::vector<Capture>& captures,
  bool at_start = true) const;
size_t find_prefix(
  const std::string_view& utf8_str,
  std::vector<Capture>& captures,
  MatchContext& ctx,
  bool at_start = true) const;
SearchResult search(
  const std::string_view& utf8_str,
  size_t start_pos = 0) const;
SearchResult search(
  const std::string_view& utf8_str,
  std::vector<Capture>& captures,
  MatchContext& ctx,
  size_t start_pos = 0) const;
```

- `SyntaxMode::Extended` (default) enables the full regex syntax.
  `SyntaxMode::IregexpStrict` restricts to RFC 9485 iregexp.
- `ok()` returns true when the regex compiled successfully (no errors).
- `match()` checks full-string match for the given input.
- `find_prefix()` returns the longest matching prefix length, or `npos`.
- `search()` finds the first match anywhere in the string starting from a byte
  offset. Returns a `SearchResult` with `match_start`, `match_len`, and
  `found()`. The capture-aware overload fills group spans and reuses a
  `MatchContext` across calls. Used by `TRegex::GlobalReplace`.
- Capture-aware `find_prefix()` fills group spans as byte offsets.
- `at_start` controls whether `^` can match in this invocation. This is used by
  wrapper-level scanning logic (for example global replacement probe loops).
- `MatchContext` is the reusable per-caller/per-thread scratch state for
  simulation and stats.

## Rune Constants

All constants live in namespace `trieste::regex` as `inline constexpr`.

### ASCII character constants

Named aliases for ASCII characters that have syntactic meaning in regexes:

| Constant    | Value  | Purpose                  |
|-------------|--------|--------------------------|
| `Backslash` | `'\\'` | Escape character         |
| `LParen`    | `'('`  | Open group               |
| `RParen`    | `')'`  | Close group              |
| `Pipe`      | `'|'`  | Alternation syntax       |
| `Question`  | `'?'`  | Zero-or-one syntax       |
| `Asterisk`  | `'*'`  | Zero-or-more syntax      |
| `Plus`      | `'+'`  | One-or-more syntax       |

### Operator sentinel runes

Internal operator tokens placed in the postfix stream. Their values are in the
range `0xAFFF00–0xAFFFFF`, well outside Unicode, so they cannot collide with
literal runes (including escaped operators like `\*`):

| Sentinel      | Value      | Arity  | Precedence |
|---------------|------------|--------|------------|
| `Catenation`  | `0xAFFF00` | Binary | 2 (middle) |
| `Alternation` | `0xAFFF01` | Binary | 1 (lowest) |
| `ZeroOrOne`   | `0xAFFF02` | Unary  | 3 (highest, emitted immediately) |
| `ZeroOrMore`  | `0xAFFF03` | Unary  | 3 (highest, emitted immediately) |
| `OneOrMore`   | `0xAFFF04` | Unary  | 3 (highest, emitted immediately) |
| `Split`       | `0xAFFF05` | —      | NFA-only (epsilon state) |
| `WordBoundary`| `0xAFFF06` | Unary  | Conditional epsilon (`\b`) |
| `LazyZeroOrOne` | `0xAFFF07` | Unary | Lazy quantifier `??` |
| `LazyZeroOrMore`| `0xAFFF08` | Unary | Lazy quantifier `*?` |
| `LazyOneOrMore` | `0xAFFF09` | Unary | Lazy quantifier `+?` |
| `StartAnchor` | `0xAFFF0A` | Unary  | Conditional epsilon (`^`) |
| `EndAnchor`   | `0xAFFF0B` | Unary  | Conditional epsilon (`$`) |
| `Match`       | `0xAFFFFF` | —      | NFA-only (accept state) |

### Class-ref sentinels

A label in the range `[RuneClassBase, RuneClassMax]` (`0xBF0000–0xBFFFFF`)
is not a literal rune but an index into `RegexEngine::rune_classes_`. This
allows `StateDef` to remain unchanged — a class-ref is just a label value
that `step()` dispatches differently.

| Constant        | Value      | Purpose |
|-----------------|------------|---------|
| `RuneClassBase` | `0xBF0000` | Start of class-ref index space |
| `RuneClassMax`  | `0xBFFFFF` | End of class-ref index space (64K classes) |

Helper functions: `is_class_ref(label)`, `class_ref_index(label)`.

### Resource limits (RFC 9485 §8)

| Constant         | Value    | Enforced in |
|------------------|----------|-------------|
| `MaxRepetition`  | `1000`   | Range quantifier parsing |
| `MaxPostfixSize` | `100000` | End of shunting-yard + range expansion |
| `MaxStates`      | `100000` | Start of `postfix_to_nfa` |
| `MaxCaptures`    | `64`     | Capturing group indexing |
| `MaxGroupNesting`| `256`    | Nested group depth during parsing |
| `MaxClosureCacheEntries` | `1000000` | `precompute_epsilon_closures` |

## Utility Functions

### `decode_rune(utf8_str, pos)`

```cpp
inline std::pair<rune_t, size_t>
decode_rune(const std::string_view& utf8_str, size_t pos);
```

Decodes one rune from `utf8_str` at byte offset `pos`. Returns
`{rune_value, bytes_consumed}`. **ASCII fast path**: if the byte at `pos` is
< 128, returns immediately without calling `utf8_to_rune`. Used by all
simulation loops — callers must ensure `pos < utf8_str.size()`.

## Key Data Structures

### `RuneClass`

A sorted, non-overlapping set of `[lo, hi]` inclusive `rune_t` intervals.
Used to represent dot (`.`), character classes (`[a-z]`), and Unicode
categories (`\p{L}`).

Fields:
- `ranges` — `std::vector<std::pair<rune_t, rune_t>>` of sorted intervals.
- `ascii_bitmap[2]` — `uint64_t[2]` bit-per-codepoint bitmap for runes
  0–127. `ascii_bitmap[r >> 6] >> (r & 63) & 1` tests membership in O(1).
  Rebuilt automatically by `normalize()`, `complement()`, and `dot()`.

Methods:
- `contains(rune_t)` — ASCII bitmap check for `r < 128`, else O(log k)
  binary search on `ranges`.
- `add_range(lo, hi)` — push a range without normalizing. Call `finalize()`
  when done.
- `merge(other)` — append another class's ranges without normalizing. Call
  `finalize()` when done.
- `finalize()` — sort, merge overlapping ranges, and rebuild the ASCII
  bitmap. Must be called after all `add_range`/`merge` calls are complete,
  before the class is used for matching or `complement()`.
- `complement()` — complement over Unicode scalar values (0–0xD7FF,
  0xE000–0x10FFFF), excluding surrogates. Requires finalized input.
- `dot()` — static factory returning all Unicode scalar values.

Private:
- `rebuild_ascii_bitmap()` — scans `ranges` and sets bits for runes 0–127.

`RuneClass` objects are stored in a `std::vector<RuneClass> rune_classes_`
member on `RegexEngine`. `make_class_ref(rc)` appends a class and returns
the corresponding class-ref sentinel for use in the postfix stream.

### `StateDef`

Each NFA state is a `StateDef`, stored contiguously in a pre-reserved
`std::vector<StateDef> owned_states_`. Referenced internally via raw pointer
alias `State = StateDef*`.

Fields:
- `label` — the rune this state matches, a class-ref sentinel (dispatched via
  `RuneClass::contains()`), or `Split`/`Match` for control states.
- `next` — primary successor state.
- `next_alt` — secondary successor (used only by `Split` states).
- `closure_index` — dense state index for closure cache and dedup.
- `ascii_accept[2]` — `uint64_t[2]` per-state bitmap. Populated by
  `finalize_states()`: class-ref states copy their RuneClass's
  `ascii_bitmap`, literal states with label < 128 set the single
  corresponding bit, all others remain zero. Enables `step()` to bypass
  `is_class_ref`/`class_ref_index`/`contains` for ASCII runes.
- `trivial_closure` — `bool`, set by `precompute_epsilon_closures()`.
  True when the state's epsilon closure is `{self}` for all 8 flag
  combinations (always true for literal/class-ref/Match states). Enables
  `add_state()` to skip the closure cache lookup.

Ownership/lifetime model:

- The constructor computes the postfix first, then reserves
  `owned_states_` with capacity `2 * postfix.size() + 1` to guarantee no
  reallocation during Thompson construction. This keeps raw `State`
  pointers (including `Frag::dangling` targets) stable.
- `create_state()` appends a `StateDef` to `owned_states_` and returns a
  raw pointer to it.
- All construction temporaries (the fragment stack, closure-computation
  epoch vectors) are function-local — they exist only during the
  constructor call and are destroyed automatically.
- `RegexEngine` is explicitly non-copyable and movable to keep raw-pointer
  ownership semantics safe and obvious.

### `MatchContext`

`MatchContext` is public API with private internals (friend to `RegexEngine`).
It holds per-call mutable state:

- `noncapturing_current_states`, `noncapturing_next_states` — scratch vectors
  for non-capturing simulation.
- `visited_states` — per-state epoch vector for dedup (used when > 128
  states).
- `visited_bits_[2]` — `uint64_t[2]` bitset for dedup when ≤ 128 states
  (`BitsetMaxStates`). Faster than epoch-based dedup for small NFAs.
- `use_bitset_` — chosen at `bind_engine()` time based on `state_count`.
- `capture_frames` — contiguous arena for capture slot storage.
- `capture_traversal_stack_` — `std::vector<std::pair<State, size_t>>`
  reusable stack for iterative `add_state_capturing`.
- `epoch_counter` — monotonic counter for epoch-based dedup.
- Optional `MatchStats` (gated by `TRIESTE_REGEX_ENGINE_ENABLE_STATS`).

Reuse model:

- Reuse a context across calls in one caller/thread.
- Do not use the same context concurrently across threads.
- Each match call binds the context to the engine, allowing safe context reuse
  across different compiled engine instances.

### `MatchStats`

Optional counters (gated by `TRIESTE_REGEX_ENGINE_ENABLE_STATS`):

- `rune_steps` — total input positions processed.
- `active_states_total` — cumulative active states across steps.
- `max_active_states` — peak active state count.
- `class_ref_checks` — class-ref dispatch count (non-ASCII path only).
- `literal_checks` — literal label comparisons (non-ASCII path only).

### `Capture` and `Thread`

- `Capture` stores `[start, end)` byte offsets and a `matched()` helper.
- Capture-aware simulation uses `Thread { state, caps_frame }` where
  `caps_frame` indexes a contiguous capture-frame arena.
- Capture slot storage is held in MatchContext's frame arena with fixed frame
  width `2 * num_captures_`.
- Capture open/close epsilon transitions clone frame contents, update one slot,
  and continue traversal so sibling branches keep independent capture state.

### `Frag`

A fragment of an NFA under construction:

- `start` — the entry state of the fragment.
- `dangling` — a `std::list<State*>` of raw pointers into the `next` or
  `next_alt` members of states within the fragment. `patch()` sets them all
  to point to a given target state.

### `ClosureSpan`

A lightweight view into the flat closure cache:

- `data` — pointer into `closure_cache_flat_`.
- `size` — number of states in this closure.

Returned by `epsilon_closure_cached()`.

## Phase Details

### Phase 1: Shunting-yard (`regexp_to_postfix_runes`)

Converts infix regex syntax to postfix tokens with:

- implicit concatenation (`Catenation`)
- alternation (`Alternation`)
- quantifiers (`?`, `*`, `+`, `{n}`, `{n,m}`, `{n,}`)
- lazy quantifiers (`??`, `*?`, `+?`)
- assertions (`\b`, `^`, `$`)
- groups (capturing and non-capturing `(?:...)`)
- class refs (dot, bracket classes, unicode categories, shorthands)

#### Escape Handling

The parser accepts these escapes:

| Pattern | Result |
|---------|--------|
| `\n`, `\r`, `\t` | Literal runes 0x0A, 0x0D, 0x09 |
| `\p{Xx}` | Unicode General Category lookup via `unicode_data.h`; emits class-ref |
| `\P{Xx}` | Same but complemented |
| `\d`, `\D`, `\s`, `\S`, `\w`, `\W` | Shorthand class refs |
| `\xNN` | Byte-valued hex escape |
| `\b` | Word-boundary assertion |
| `\(`, `\)`, `\*`, `\+`, `\-`, `\.`, `\?`, `\[`, `\\`, `\]`, `\^`, `\$`, `\{`, `\|`, `\}`, `\/`, `\,` | Literal rune |
| Any other escape | **malformed** |
| Trailing `\` | **malformed** |

Inside bracket classes, unicode categories and shorthand classes are merged
into the bracket class.

#### Range Quantifiers

Range quantifiers (`{n}`, `{n,m}`, `{n,}`) are handled by **postfix
expansion** — the atom's postfix tokens are extracted and replicated. This
keeps `postfix_to_nfa` unchanged. Tracking variables:

| Variable | Purpose |
|----------|---------|
| `atom_start` | Index in postfix where the current atom begins |
| `group_start_stack` | Saves `(postfix_index, need_concat)` on `(` |
| `need_concat_before_atom` | Whether a Cat was pushed before this atom |
| `atom_is_quantified` | Detects nested quantifiers (→ malformed) |

Expansion formula (postfix notation, Cat is the `Catenation` operator):

| Pattern | Postfix expansion |
|---------|-------------------|
| `a{n}` | `a × n` chained with Cat |
| `a{n,m}` | `a × n` + `(a ZeroOrOne Cat) × (m−n)` |
| `a{n,}` | `a × n` + `a ZeroOrMore Cat` |
| `a{0}` | Atom removed, preceding Cat undone |

Nested quantifiers (e.g. `a*{2}`) are rejected as malformed. Expansion size
is checked upfront against `MaxPostfixSize`.

#### Rejected Syntax

The following patterns are rejected as malformed:

- Bare `]` or `}` outside their context.
- Empty character class `[]` or `[^]`.
- Inverted ranges `[z-a]`.
- Range quantifiers exceeding `MaxRepetition`.
- Unterminated constructs (`[abc`, `a{`, `a\`).
- Unknown/unsupported escapes.

### Phase 2: Thompson's construction (`postfix_to_nfa`)

Walks the postfix runes left-to-right, maintaining a stack of `Frag` objects:

| Postfix token | Action |
|---------------|--------|
| Literal rune or class-ref | Create a new state, push fragment |
| `Catenation`  | Pop two, patch left's dangling to right's start |
| `Alternation` | Pop two, create Split pointing to both starts |
| `ZeroOrOne`   | Pop one, create Split with one branch to fragment |
| `ZeroOrMore`  | Pop one, create Split looping back |
| `OneOrMore`   | Pop one, create Split looping back, entry is original start |

The postfix size is checked against `MaxStates` before construction begins.
Stack underflow sets `error_code_` and returns the accept state as a safe
sentinel.

### Phase 3: Finalization

Two functions run after Thompson construction:

**`precompute_epsilon_closures()`**: For every state and for all 8 flag
combinations (boundary × at_start × at_end), computes the full epsilon
closure and stores it in a flat array (`closure_cache_flat_`) indexed via
an offset table (`closure_cache_offsets_`). Also sets `trivial_closure =
true` on any state whose closure is `{self}` for all 8 combinations. Total
closure entries are bounded by `MaxClosureCacheEntries`. The epoch vector
and counter used for cycle detection are local to this function.

**`finalize_states()`**: Populates `ascii_accept[2]` on each state:
class-ref states copy the RuneClass's `ascii_bitmap`, literal states with
label < 128 set their single bit, all others stay zero. Since
`owned_states_` is already contiguous (pre-reserved `vector<StateDef>`),
no copy or pointer remapping is needed.

### Phase 4: Simulation (`match`, `find_prefix`)

Uses the standard Thompson NFA simulation with several fast paths:

1. `start_list` seeds the initial active set via epsilon closure.
2. For each input rune, `step` builds the next state set. The loop has two
   branches:
   - **ASCII fast path** (`rune < 128`): for each active state, test the
     per-state `ascii_accept` bitmap. This single bit-test replaces the
     entire class-ref dispatch chain (`is_class_ref` → `class_ref_index` →
     `rune_classes_[]` → `RuneClass::contains()`).
   - **Non-ASCII fallback**: for each active state, dispatch via
     `is_class_ref(label)` + `contains(rune)` or literal equality.
3. Conditional epsilon states use booleans threaded through simulation:
   - `boundary_match` for `\b`
   - `at_start` for `^`
   - `at_end` for `$`
4. `add_state()` has a **trivial-closure shortcut**: if `state->trivial_closure`
   is true, the state is added directly with dedup, bypassing the closure
   cache lookup entirely. Otherwise the flat closure cache is consulted.
5. Input decoding uses `decode_rune()` which has an ASCII fast path avoiding
   the full `utf8_to_rune` call for bytes < 128.

**Conditional detection**: `detect_conditional_states()` scans the NFA for
`WordBoundary`, `StartAnchor`, or `EndAnchor`. When absent, simulation
skips `is_word_char()` calls and passes constant `false` for all
boundary/anchor parameters, providing separate code paths for conditional
and non-conditional patterns.

**Capture-aware simulation** (`add_state_capturing`): Iterative (stack-based)
epsilon traversal using `capture_traversal_stack_` in MatchContext. Pushes
`next_alt` before `next` so `next` is popped first (greedy DFS ordering).
Capture open/close transitions clone the capture frame and update one slot
before continuing. The stack is reused across calls via MatchContext.

**Epoch-based deduplication**: For NFAs with > 128 states, MatchContext
tracks per-state epochs in a visited-state vector. A per-context epoch
counter increments before each step, and `add_state` skips states already
marked in the current epoch. For NFAs with ≤ 128 states, a 128-bit bitset
(`visited_bits_[2]`) is used instead for faster clear and test.

## Error Handling

The engine uses an `error_code_` field (type `ErrorCode`) rather than
exceptions or error tokens:

- Set during `regexp_to_postfix_runes` (invalid syntax, invalid escapes,
  resource limits) or `postfix_to_nfa` (stack underflow, state limit).
- Once set, `match()` returns false immediately.
- Callers should check `ok()` after construction, or inspect `error_code()`
  and `error()` for a human-readable message.

## Performance Notes

### Compile+match advantage

Trieste's NFA compile is much cheaper than RE2's DFA. On benchmarks, the
compile+match ratio (`cm_ratio`) is ~0.31x (Trieste ~3x faster than RE2
overall). This makes the engine well-suited for single-use patterns.

### Match-only gap

The match-only ratio (`m_ratio`) is ~2.6x. This is structural: RE2's DFA
does O(1) work per rune after warmup, while NFA simulation is O(active_states)
per rune. The gap is largest on patterns with many steps and high active
state counts.

### Key optimizations

- **Per-state ASCII bitmap**: `ascii_accept[2]` on StateDef unifies
  class-ref and literal dispatch into a single bit-test for runes < 128.
  Eliminates the `is_class_ref` → `class_ref_index` → `rune_classes_[]` →
  `contains()` chain for the common case.
- **RuneClass ASCII bitmap**: `ascii_bitmap[2]` on RuneClass provides O(1)
  membership test for runes < 128, avoiding binary search on `ranges`.
  Unicode category bitmaps are precomputed at compile time in
  `unicode_data.h`.
- **Trivial-closure shortcut**: States whose epsilon closure is always
  `{self}` (literal, class-ref, Match states) skip the closure cache
  lookup in `add_state()`.
- **ASCII `decode_rune` bypass**: `decode_rune()` returns immediately for
  bytes < 128 without calling `utf8_to_rune`.
- **Bitset dedup**: NFAs with ≤ 128 states use a 128-bit bitset for
  visited-state tracking instead of epoch-based vector dedup.
- **Flat closure cache**: `closure_cache_flat_` + `closure_cache_offsets_`
  store precomputed epsilon closures in contiguous memory, indexed by
  `(closure_index << 3) | flags`.
- **Pre-reserved contiguous storage**: `owned_states_` is a
  `vector<StateDef>` reserved upfront based on postfix size, so all states
  are contiguous in memory for cache-friendly simulation without a separate
  compaction pass.
- **Iterative capturing traversal**: `add_state_capturing` uses an explicit
  stack (`capture_traversal_stack_` in MatchContext) instead of recursion,
  eliminating per-epsilon function-call overhead.
- **Capture-frame arena**: capture-aware simulation avoids per-thread vector
  copies by storing capture slots in a contiguous arena.
- **Non-capturing scratch reuse**: `match()` and non-capturing
  `find_prefix()` reuse MatchContext vectors to avoid per-call allocations.
- **Optional stats**: `TRIESTE_REGEX_ENGINE_ENABLE_STATS` gates counters
  stored in MatchContext and exposed via `MatchContext::stats()`.

## `unicode_data.h` Extensions

Beyond the auto-generated Unicode 15.1 General Category range tables,
`unicode_data.h` contains compile-time ASCII bitmap infrastructure:

- `AsciiBitmap` — struct with `uint64_t words[2]`.
- `compute_ascii_bitmap<N>(ranges)` — `constexpr` template that scans a
  range table and produces an `AsciiBitmap` for runes 0–127.
- 36 `inline constexpr AsciiBitmap` constants (one per Unicode General
  Category), precomputed at compile time.
- `CategoryInfo` struct extended with `AsciiBitmap ascii_bitmap` field.
- `find_category()` returns precomputed bitmaps alongside range data.

These precomputed bitmaps are used when constructing a `RuneClass` from a
Unicode category, avoiding runtime bitmap computation.

## Behavior Notes

- Empty pattern compiles to an accept-only NFA.
- `find_prefix()` can produce zero-length matches.
- Anchors are implemented in-engine (`^`, `$`) and do not require wrapper-only
  emulation.
- The engine operates on UTF-8 codepoint decoding, but capture offsets are byte
  offsets (matching existing parser/rewrite expectations).

## Additional Syntax Coverage

Beyond the core iregexp-compatible pieces, parser support includes:

- **Capturing and non-capturing groups**: `( ... )`, `(?: ... )`.
- **Capture operators in postfix/NFA**: `CaptureGroup + i` wraps fragments with
  epsilon `CaptureOpen + i` and `CaptureClose + i` states.
- **Lazy quantifiers**: `??`, `*?`, `+?` (implemented by swapped `Split`
  branch preference in Thompson construction).
- **POSIX classes inside brackets**: `[:alpha:]`, `[:digit:]`, `[:alnum:]`,
  `[:blank:]`, `[:space:]`, `[:xdigit:]`, `[:upper:]`, `[:lower:]`,
  `[:print:]`, `[:graph:]`, `[:cntrl:]`, `[:punct:]`, `[:ascii:]`.

## Conventions

- **Namespace**: `trieste::regex` for the engine; `trieste::unicode` for
  category data.
- **`inline constexpr`** for namespace-scope rune constants and resource
  limits.
- **`StateDef*`** aliased as `State`; owned by `owned_states_` (a
  pre-reserved `vector<StateDef>`).
- **Thread-safety model**: Engine matching methods are `const` and engine state
  is read-only after construction. Thread safety depends on context usage:
  sharing one `MatchContext` across threads is unsafe; using separate contexts
  per thread is safe.
- **Header-only**: The engine is `regex_engine.h`; category data is in
  `unicode_data.h` (auto-generated from Unicode 15.1 `UnicodeData.txt`).

## Modification Guidelines

When extending the regex engine:

1. **New set-based matchers** (e.g. new Unicode properties): Create a
   `RuneClass`, store it via `make_class_ref()`, and emit the class-ref in
   the postfix stream. No changes to `StateDef` or `postfix_to_nfa` needed.
   The ASCII bitmap will be computed automatically by `normalize()`.
2. **New operators**: Assign a sentinel rune in the `0xAFFF00` range and update
   parsing plus NFA construction.
3. **New conditional epsilon states**: Update both `add_state` and
   `add_state_capturing`, and thread required context booleans through
   `start_list`/`step`/`find_prefix` call paths. Update the 3-bit flag
   encoding in `closure_flags()` and increase the slot multiplier from 8.
4. **Testing**: Add test cases to `test/regex_engine_test.cc`. Test positive
   matches, negative matches, and malformed pattern rejection for any new
   error conditions. For assertions, include both full match and prefix/probe
   scenarios.
5. **Benchmarking**: Run `test/regex_engine_benchmark.cc` to verify
   performance impact. Compare `cm_ratio` and `m_ratio` against baseline.
   To produce a chart and markdown report from multiple benchmark passes, run:
   ```
   .venv/bin/python .github/skills/regex-engine/benchmark_chart.py
   ```
   This script runs the benchmark N times (default 5, with warmup), computes
   medians, and outputs a PNG chart + markdown report. Pass `--input <json>`
   to re-render from previously saved data without re-running benchmarks.
6. **Keep phases separate**: Parsing (shunting-yard), construction (Thompson),
   compaction (closures + arena), and simulation are cleanly separated.
   Changes should respect these boundaries.
7. **Unicode data**: If upgrading the Unicode version, regenerate
   `unicode_data.h` from the new `UnicodeData.txt`. Remember to also update
   the `compute_ascii_bitmap` constants and `CategoryInfo` entries.
