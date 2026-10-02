# Fuzzing

Every Trieste-based tool built with `Driver` gets a `test` subcommand for
free. It stress-tests the pass pipeline by generating random trees that
conform to each pass's input well-formedness (WF) spec, running the pass,
and checking that the result conforms to the pass's output WF spec. This
finds passes that mishandle input shapes no one thought to write by hand,
without needing any test files.

This document covers running fuzzing from the command line. For the WF specs
and passes it exercises, and for driving `Fuzzer` directly from C++, see the
[reference documentation](/docs/reference.md).

## Running

```sh
my_tool test
```

With no arguments, this tests every pass that has both an input and an
output WF spec attached (passes without one are skipped, since there's
nothing to generate from or check against), using 100 randomly generated
trees per pass.

To restrict testing to a range of passes, name the first and last pass to
test (omitting the last pass tests only a single pass):

```sh
my_tool test parse_literals desugar
```

## Reading the output

For each pass tested, Trieste reports how many hash-unique trees it managed
to generate (and how many retries that took, see `--max_retries` below),
followed by a breakdown of what happened to them:

* **well-formedness errors** — the pass produced a tree that violates its
  output WF spec. This is what fuzzing looks for; each one is reported with
  the seed that produced it (see [Reproducing a
  failure](#reproducing-a-failure)).
* **stopped by errors** — the pass legitimately emitted an `Error` node
  (e.g. an unbound identifier), so no WF check applies. These are grouped by
  message and counted.
* **survivors** — trees that passed the WF check. Reported when the run
  produced at least one error, or with `--sequence` (see below).

Whenever a well-formedness error is encountered, Trieste prints why the WF check
failed, together with the generated input tree and the tree after applying the
failing pass. If many trees are causing well-formedness errors you can use the
flag `-f` to stop after the first error.

## Flags

| Flag | Description |
|------|-------------|
| `-c, --seed_count` | Number of trees to generate per pass (default 100). |
| `-s, --seed` | Starting random seed. Random by default; set it to make a run reproducible. |
| `-d, --max_depth` | Roughly how deep generated trees should be (default 10). Trieste steers generation toward terminating once trees grow past this, so it's a target rather than a hard cap. |
| `-f, --failfast` | Stop at the first well-formedness failure. |
| `--sequence` | Instead of generating a fresh tree per pass, run each pass on the survivors of the previous one. This exercises passes on trees actually shaped by earlier passes, rather than on every tree the WF spec alone admits. |
| `--size_stats` | Collect and log tree size/height statistics (turns on `Info`-level logging if no log level is set). |
| `-r, --max_retries` | How many times to retry generation, with a new seed, if it produces a tree identical (by hash) to one already generated for this pass (default `seed_count * 2`). Once exhausted, that iteration is skipped. |
| `--gen_bound` | Whether generated trees should try to reuse an existing bound name instead of a fresh one, so lookups get exercised too (default on). |
| `--weights <tok> <n> [<tok> <n> ...]` | Bias generation towards or away from specific tokens; see [Token weights](#token-weights). |
| `-l, --log_level` | `Trace`, `Debug`, `Info`, `Warning`, `Output`, `Error`, or `None`. `Trace` prints every generated tree and its output, not just failures. |

## Reproducing a failure

A failure is reported with the pass name and the seed that produced it.
In order to reproduce a failing generation, rerun with that seed, restricted
to the failing pass and a single iteration:

```sh
my_tool test my_pass my_pass -s 123456789 -c 1
```

## Token weights

By default every candidate token at a given point in the tree is equally
likely to be generated. `--weights` lets you skew that, which is useful for
steering fuzzing towards constructs that are under-tested, or away from
ones that blow up tree size (e.g. deeply recursive expression forms):

```sh
my_tool test --weights my_tool-paren 1 my_tool-ident 5
```

Weights are given as `<token> <weight>` pairs, using the name the token was
registered with in `TokenDef` (not necessarily its C++ identifier). Higher
numbers make a token more likely relative to its siblings; a weight of `0`
excludes it. If every candidate at some point in the tree ends up with
weight `0`, that seed can't produce a valid tree and generation reports an
error.

## Checking randomness

For reproducability across platforms, Trieste implements its own random number
generator. For regression checking, it is possible to measure the entropy of
that generator:

```sh
my_tool test debug_entropy
```

This samples the random number generator itself across `seed_count` seeds and
reports the entropy of the values produced. This is a sanity check on randomness,
not on any WF spec so most users won't need it.

## Related: pattern checking

The `check` subcommand performs a separate, static check: it looks for
rewrite-rule patterns that mention tokens not covered by the surrounding WF
spec, which usually indicates a typo or a rule that can never match. It
doesn't generate or run anything, unlike `test`. See `my_tool check --help`
for its options.
